param(
	[string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
	[string]$Endpoint = 'http://127.0.0.1:8080/mcp',
	[string]$OutDir = '',
	[int]$TimeoutSec = 10,
	[switch]$AllowOffline,
	[switch]$SkipAliasCalls,
	[switch]$SkipSafetyProbes
)

$ErrorActionPreference = 'Stop'

if (-not $OutDir) {
	$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
	$OutDir = Join-Path $Root "Tmp\Validation\ReleasePreflight\$timestamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$summary = [ordered]@{
	timestamp = (Get-Date).ToString('s')
	root = $Root
	endpoint = $Endpoint
	out_dir = $OutDir
	allow_offline = [bool]$AllowOffline
	checks = @()
	runtime = [ordered]@{}
	alias_calls = @()
	safety_probes = @()
	success = $false
}

function Add-Check {
	param(
		[string]$Name,
		[string]$Status,
		[object]$Details = $null
	)
	$summary.checks += [ordered]@{
		name = $Name
		status = $Status
		details = $Details
	}
}

function Save-Json {
	param(
		[string]$Name,
		[object]$Value
	)
	$path = Join-Path $OutDir $Name
	$Value | ConvertTo-Json -Depth 80 | Set-Content -LiteralPath $path -Encoding UTF8
	return $path
}

$script:NextJsonRpcId = 1
function Invoke-McpRequest {
	param(
		[string]$Method,
		[object]$Params = $null,
		[string]$SessionId = ''
	)

	$body = [ordered]@{
		jsonrpc = '2.0'
		id = $script:NextJsonRpcId
		method = $Method
	}
	$script:NextJsonRpcId++
	if ($null -ne $Params) {
		$body.params = $Params
	}

	$headers = @{}
	if ($SessionId) {
		$headers['Mcp-Session-Id'] = $SessionId
	}

	$json = $body | ConvertTo-Json -Depth 80
	$response = Invoke-WebRequest -Uri $Endpoint -Method Post -ContentType 'application/json' -Headers $headers -Body $json -TimeoutSec $TimeoutSec -UseBasicParsing
	$parsed = $response.Content | ConvertFrom-Json
	return [pscustomobject]@{
		status_code = [int]$response.StatusCode
		headers = $response.Headers
		body = $parsed
		raw = $response.Content
	}
}

function Invoke-ToolCall {
	param(
		[string]$Name,
		[hashtable]$Arguments,
		[string]$SessionId = ''
	)

	$params = @{
		name = $Name
		arguments = $Arguments
	}
	return Invoke-McpRequest -Method 'tools/call' -Params $params -SessionId $SessionId
}

function Get-ToolCallIsError {
	param([object]$Response)
	if ($null -eq $Response.body.result) {
		return $true
	}
	if ($null -eq $Response.body.result.isError) {
		return $false
	}
	return [bool]$Response.body.result.isError
}

function Get-ToolText {
	param([object]$Response)
	if ($null -eq $Response.body.result -or $null -eq $Response.body.result.content) {
		return ''
	}
	$textItems = @()
	foreach ($item in @($Response.body.result.content)) {
		if ($null -ne $item.text) {
			$textItems += [string]$item.text
		}
	}
	return ($textItems -join "`n")
}

$staticAliasScript = Join-Path $Root 'Validation\Smoke\VerifyCompatibilityAliases.ps1'
try {
	$staticOutput = & powershell -ExecutionPolicy Bypass -File $staticAliasScript -Root $Root 2>&1
	$staticText = ($staticOutput | Out-String).Trim()
	Save-Json -Name 'static-alias-smoke.json' -Value ([ordered]@{ output = $staticText; exit_code = $LASTEXITCODE }) | Out-Null
	if ($LASTEXITCODE -ne 0) {
		Add-Check -Name 'static-alias-smoke' -Status 'failed' -Details $staticText
		throw "Static compatibility alias smoke failed."
	}
	Add-Check -Name 'static-alias-smoke' -Status 'passed' -Details $staticText
}
catch {
	Add-Check -Name 'static-alias-smoke' -Status 'failed' -Details $_.Exception.Message
	Save-Json -Name 'summary.json' -Value $summary | Out-Null
	throw
}

$sessionId = ''
try {
	$initializeParams = @{
		protocolVersion = '2025-06-18'
		capabilities = @{}
		clientInfo = @{
			name = 'uebmcp-release-preflight'
			version = '1'
		}
	}
	$init = Invoke-McpRequest -Method 'initialize' -Params $initializeParams
	Save-Json -Name 'initialize.json' -Value $init.body | Out-Null
	if ($init.headers.ContainsKey('Mcp-Session-Id')) {
		$sessionId = [string]$init.headers['Mcp-Session-Id']
	}
	elseif ($init.headers.ContainsKey('mcp-session-id')) {
		$sessionId = [string]$init.headers['mcp-session-id']
	}
	$registeredCount = [int]$init.body.result.capabilities.tools.registeredCount
	$summary.runtime.registered_count = $registeredCount
	$summary.runtime.session_id = $sessionId
	Add-Check -Name 'runtime-initialize' -Status 'passed' -Details @{ registered_count = $registeredCount; session_id = $sessionId }
}
catch {
	$detail = $_.Exception.Message
	if ($AllowOffline) {
		$summary.runtime.offline = $true
		$summary.runtime.offline_error = $detail
		Add-Check -Name 'runtime-initialize' -Status 'skipped' -Details "Endpoint offline: $detail"
		$summary.success = $true
		Save-Json -Name 'summary.json' -Value $summary | Out-Null
		Write-Host "Release preflight completed static checks only because the endpoint is offline and -AllowOffline was set." -ForegroundColor Yellow
		Write-Host "Summary: $OutDir\summary.json"
		exit 0
	}

	Add-Check -Name 'runtime-initialize' -Status 'failed' -Details $detail
	Save-Json -Name 'summary.json' -Value $summary | Out-Null
	throw "Could not reach MCP endpoint '$Endpoint'. Start the UE editor MCP server or rerun with -AllowOffline for static-only validation. $detail"
}

$tools = @()
$toolNames = @()
try {
	$toolsResponse = Invoke-McpRequest -Method 'tools/list' -SessionId $sessionId
	Save-Json -Name 'tools-list.json' -Value $toolsResponse.body | Out-Null
	$tools = @($toolsResponse.body.result.tools)
	$toolNames = @($tools | ForEach-Object { $_.name })
	$summary.runtime.tools_list_count = $tools.Count
	if ($tools.Count -ne $summary.runtime.registered_count) {
		Add-Check -Name 'runtime-tools-list-count' -Status 'failed' -Details @{ tools_list = $tools.Count; registered_count = $summary.runtime.registered_count }
	}
	else {
		Add-Check -Name 'runtime-tools-list-count' -Status 'passed' -Details @{ count = $tools.Count }
	}
}
catch {
	Add-Check -Name 'runtime-tools-list' -Status 'failed' -Details $_.Exception.Message
}

foreach ($method in @('resources/list', 'prompts/list')) {
	try {
		$response = Invoke-McpRequest -Method $method -SessionId $sessionId
		$fileName = ($method -replace '/', '-') + '.json'
		Save-Json -Name $fileName -Value $response.body | Out-Null
		$key = if ($method -eq 'resources/list') { 'resources' } else { 'prompts' }
		$count = @($response.body.result.$key).Count
		$summary.runtime[$key + '_count'] = $count
		Add-Check -Name "runtime-$($method -replace '/', '-')" -Status 'passed' -Details @{ count = $count }
	}
	catch {
		Add-Check -Name "runtime-$($method -replace '/', '-')" -Status 'failed' -Details $_.Exception.Message
	}
}

$expectedAliases = @(
	'get_project_info',
	'execute_python',
	'search_project',
	'get_selection',
	'set_viewport_camera',
	'run_console_command',
	'get_render_stats',
	'get_memory_report',
	'get_skeleton_info',
	'create_blend_space',
	'get_static_mesh_info',
	'build_navigation',
	'get_replication_info',
	'create_common_ui_widget',
	'get_world_partition_info',
	'list_pcg_graphs',
	'generate_ui_image',
	'generate_3d_model'
)
$missingAliases = @($expectedAliases | Where-Object { $toolNames -notcontains $_ })
if ($missingAliases.Count -gt 0) {
	Add-Check -Name 'runtime-expected-aliases' -Status 'failed' -Details @{ missing = $missingAliases }
}
else {
	Add-Check -Name 'runtime-expected-aliases' -Status 'passed' -Details @{ checked = $expectedAliases.Count }
}

if (-not $SkipAliasCalls) {
	$aliasSmokeCalls = @(
		@{ name = 'get_project_info'; arguments = @{} },
		@{ name = 'search_project'; arguments = @{ query = 'UEBridgeMCP'; limit = 5 } },
		@{ name = 'get_selection'; arguments = @{} },
		@{ name = 'get_level_info'; arguments = @{} },
		@{ name = 'get_spatial_context'; arguments = @{} },
		@{ name = 'get_render_stats'; arguments = @{} },
		@{ name = 'get_memory_report'; arguments = @{} },
		@{ name = 'set_viewport_camera'; arguments = @{ action = 'query' } },
		@{ name = 'run_console_command'; arguments = @{ command = 'stat fps'; dry_run = $true } }
	)

	$firstLevelActorHandle = $null
	foreach ($call in $aliasSmokeCalls) {
		if ($toolNames -notcontains $call.name) {
			$summary.alias_calls += [ordered]@{ name = $call.name; status = 'skipped'; reason = 'alias not visible in tools/list' }
			continue
		}

		try {
			if ($call.name -eq 'get_spatial_context') {
				if ($null -eq $firstLevelActorHandle) {
					$levelResponse = Invoke-ToolCall -Name 'get_level_info' -Arguments @{} -SessionId $sessionId
					$levelActors = @($levelResponse.body.result.structuredContent.actors)
					if ($levelActors.Count -gt 0) {
						$firstLevelActorHandle = $levelActors[0].handle
					}
				}

				if ($null -eq $firstLevelActorHandle) {
					$summary.alias_calls += [ordered]@{ name = $call.name; status = 'skipped'; reason = 'no actor handle available for spatial context smoke' }
					continue
				}

				$call['arguments'] = @{
					actors = @(@{ actor_handle = $firstLevelActorHandle })
					include_pairwise_distances = $false
					include_ground_hits = $false
				}
			}

			$response = Invoke-ToolCall -Name $call.name -Arguments $call.arguments -SessionId $sessionId
			$fileName = "alias-call-$($call.name).json"
			Save-Json -Name $fileName -Value $response.body | Out-Null
			$isError = Get-ToolCallIsError $response
			$status = if ($isError) { 'failed' } else { 'passed' }
			$summary.alias_calls += [ordered]@{ name = $call.name; status = $status; is_error = $isError; text = Get-ToolText $response }
			if ($call.name -eq 'get_level_info' -and -not $isError) {
				$levelActors = @($response.body.result.structuredContent.actors)
				if ($levelActors.Count -gt 0) {
					$firstLevelActorHandle = $levelActors[0].handle
				}
			}
		}
		catch {
			$summary.alias_calls += [ordered]@{ name = $call.name; status = 'failed'; exception = $_.Exception.Message }
		}
	}

	$failedAliasCalls = @($summary.alias_calls | Where-Object { $_.status -eq 'failed' })
	if ($failedAliasCalls.Count -gt 0) {
		Add-Check -Name 'runtime-alias-tool-calls' -Status 'failed' -Details @{ failed = @($failedAliasCalls | ForEach-Object { $_.name }) }
	}
	else {
		Add-Check -Name 'runtime-alias-tool-calls' -Status 'passed' -Details @{ executed = @($summary.alias_calls | Where-Object { $_.status -eq 'passed' }).Count }
	}
}
else {
	Add-Check -Name 'runtime-alias-tool-calls' -Status 'skipped' -Details 'SkipAliasCalls was set'
}

if (-not $SkipSafetyProbes) {
	$safetyProbes = @(
		@{ name = 'build-and-relaunch'; arguments = @{ dry_run = $true; skip_relaunch = $true }; expect_error = $false; expected_text = '' },
		@{ name = 'run-python-script'; arguments = @{ script = 'print("preflight")'; dry_run = $true }; expect_error = $false; expected_text = '' },
		@{ name = 'run-python-script'; arguments = @{ script = 'print("preflight")' }; expect_error = $true; expected_text = 'UEBMCP_CONFIRMATION_REQUIRED' },
		@{ name = 'run-editor-command'; arguments = @{ command = 'quit'; dry_run = $true }; expect_error = $true; expected_text = 'UEBMCP_UNSAFE_EDITOR_COMMAND' },
		@{ name = 'manage-assets'; arguments = @{ actions = @(@{ action = 'delete'; asset_path = '/Game/UEBridgeMCPPreflight/MissingAsset' }) }; expect_error = $true; expected_text = 'confirm_delete=true' },
		@{ name = 'source-control-assets'; arguments = @{ actions = @(@{ action = 'revert'; asset_paths = @('/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial') }) }; expect_error = $true; expected_text = 'UEBMCP_CONFIRMATION_REQUIRED' },
		@{ name = 'source-control-assets'; arguments = @{ dry_run = $true; actions = @(@{ action = 'revert'; asset_paths = @('/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial') }) }; expect_error = $false; expected_text = '' }
	)

	foreach ($probe in $safetyProbes) {
		if ($toolNames -notcontains $probe.name) {
			$summary.safety_probes += [ordered]@{ name = $probe.name; status = 'skipped'; reason = 'tool not visible in tools/list' }
			continue
		}

		try {
			$response = Invoke-ToolCall -Name $probe.name -Arguments $probe.arguments -SessionId $sessionId
			$fileName = "safety-probe-$($probe.name -replace '[^A-Za-z0-9_-]', '_').json"
			Save-Json -Name $fileName -Value $response.body | Out-Null
			$isError = Get-ToolCallIsError $response
			$text = Get-ToolText $response
			$matchesExpectation = ($isError -eq [bool]$probe.expect_error)
			if ($probe.expected_text) {
				$matchesExpectation = $matchesExpectation -and ($text -like "*$($probe.expected_text)*")
			}
			$status = if ($matchesExpectation) { 'passed' } else { 'failed' }
			$summary.safety_probes += [ordered]@{ name = $probe.name; status = $status; expected_error = [bool]$probe.expect_error; is_error = $isError; text = $text }
		}
		catch {
			$summary.safety_probes += [ordered]@{ name = $probe.name; status = 'failed'; exception = $_.Exception.Message }
		}
	}

	$failedSafetyProbes = @($summary.safety_probes | Where-Object { $_.status -eq 'failed' })
	if ($failedSafetyProbes.Count -gt 0) {
		Add-Check -Name 'runtime-safety-probes' -Status 'failed' -Details @{ failed = @($failedSafetyProbes | ForEach-Object { $_.name }) }
	}
	else {
		Add-Check -Name 'runtime-safety-probes' -Status 'passed' -Details @{ executed = @($summary.safety_probes | Where-Object { $_.status -eq 'passed' }).Count }
	}
}
else {
	Add-Check -Name 'runtime-safety-probes' -Status 'skipped' -Details 'SkipSafetyProbes was set'
}

$failedChecks = @($summary.checks | Where-Object { $_.status -eq 'failed' })
$summary.success = $failedChecks.Count -eq 0
Save-Json -Name 'summary.json' -Value $summary | Out-Null

if ($summary.success) {
	Write-Host "Release preflight passed." -ForegroundColor Green
	Write-Host "Summary: $OutDir\summary.json"
	exit 0
}

Write-Host "Release preflight failed." -ForegroundColor Red
foreach ($check in $failedChecks) {
	Write-Host (" - {0}: {1}" -f $check.name, ($check.details | ConvertTo-Json -Depth 10 -Compress)) -ForegroundColor Red
}
Write-Host "Summary: $OutDir\summary.json"
exit 1
