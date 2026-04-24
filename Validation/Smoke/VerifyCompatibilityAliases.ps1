param(
	[string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

$sourceRoot = Join-Path $Root 'Source'
$registryHeader = Join-Path $sourceRoot 'UEBridgeMCP\Public\Tools\McpToolRegistry.h'
$registrySource = Join-Path $sourceRoot 'UEBridgeMCP\Private\Tools\McpToolRegistry.cpp'

if (-not (Test-Path -LiteralPath $sourceRoot)) {
	throw "Source root not found: $sourceRoot"
}

$sourceFiles = Get-ChildItem -Path $sourceRoot -Include '*.h','*.cpp' -Recurse -File

$toolNames = New-Object 'System.Collections.Generic.HashSet[string]'
$toolToClass = @{}
$toolPattern = 'GetToolName\(\)\s+const\s+override\s+\{\s+return\s+TEXT\("([^"]+)"\);\s+\}'
foreach ($file in $sourceFiles) {
	$currentClass = $null
	$lineNumber = 0
	foreach ($line in Get-Content -LiteralPath $file.FullName) {
		$lineNumber++
		if ($line -match 'class\s+[A-Z0-9_]*_API\s+(U[A-Za-z0-9_]+)\s*:\s*public\s+UMcpToolBase') {
			$currentClass = $matches[1]
		}
		if ($line -match $toolPattern) {
			$name = $matches[1]
			[void]$toolNames.Add($name)
			if ($currentClass) {
				$toolToClass[$name] = $currentClass
			}
		}
	}
}

$registeredClasses = New-Object 'System.Collections.Generic.HashSet[string]'
$registerPattern = 'RegisterToolClass\((U[A-Za-z0-9_]+)::StaticClass\(\)\)'
foreach ($file in $sourceFiles) {
	foreach ($match in Select-String -LiteralPath $file.FullName -Pattern $registerPattern) {
		if ($match.Line -match $registerPattern) {
			[void]$registeredClasses.Add($matches[1])
		}
	}
}

$aliases = @{}
$aliasLocations = @{}
$aliasPattern = 'RegisterToolAlias\(TEXT\("([^"]+)"\),\s*TEXT\("([^"]+)"\)\)'
$externalAssetAliasPattern = 'RegisterExternalAssetAlias\(Registry,\s*TEXT\("([^"]+)"\),'
foreach ($file in $sourceFiles) {
	foreach ($match in Select-String -LiteralPath $file.FullName -Pattern $aliasPattern) {
		if ($match.Line -match $aliasPattern) {
			$alias = $matches[1]
			$target = $matches[2]
			if (-not $aliasLocations.ContainsKey($alias)) {
				$aliasLocations[$alias] = @()
			}
			$aliasLocations[$alias] += "$($file.FullName):$($match.LineNumber)"
			if ($aliases.ContainsKey($alias) -and $aliases[$alias] -ne $target) {
				throw "Alias '$alias' maps to both '$($aliases[$alias])' and '$target'"
			}
			$aliases[$alias] = $target
		}
	}
	foreach ($match in Select-String -LiteralPath $file.FullName -Pattern $externalAssetAliasPattern) {
		if ($match.Line -match $externalAssetAliasPattern) {
			$alias = $matches[1]
			$target = 'generate-external-asset'
			if (-not $aliasLocations.ContainsKey($alias)) {
				$aliasLocations[$alias] = @()
			}
			$aliasLocations[$alias] += "$($file.FullName):$($match.LineNumber)"
			if ($aliases.ContainsKey($alias) -and $aliases[$alias] -ne $target) {
				throw "Alias '$alias' maps to both '$($aliases[$alias])' and '$target'"
			}
			$aliases[$alias] = $target
		}
	}
}

$errors = New-Object 'System.Collections.Generic.List[string]'
if (-not (Select-String -LiteralPath $registryHeader -Pattern 'RegisterToolAlias' -Quiet)) {
	$errors.Add("Registry header does not expose RegisterToolAlias")
}
if (-not (Select-String -LiteralPath $registrySource -Pattern 'ResolveToolName' -Quiet)) {
	$errors.Add("Registry source does not implement alias resolution")
}

foreach ($alias in $aliases.Keys) {
	$target = $aliases[$alias]
	if ($toolNames.Contains($alias)) {
		$errors.Add("Alias '$alias' conflicts with a canonical tool name")
	}
	if (-not $toolNames.Contains($target)) {
		$errors.Add("Alias '$alias' targets missing canonical tool '$target' at $($aliasLocations[$alias] -join ', ')")
	}
	elseif ($toolToClass.ContainsKey($target) -and -not $registeredClasses.Contains($toolToClass[$target])) {
		$errors.Add("Alias '$alias' targets canonical tool '$target', but class '$($toolToClass[$target])' is not registered")
	}
}

$expectedAliases = @(
	'get_project_info',
	'execute_python',
	'get_selection',
	'set_viewport_camera',
	'get_render_stats',
	'get_memory_report',
	'get_skeleton_info',
	'create_blend_space',
	'add_anim_notify',
	'get_static_mesh_info',
	'get_mesh_complexity_report',
	'build_navigation',
	'query_navigation_path',
	'get_replication_info',
	'set_component_replication',
	'get_behavior_tree_info',
	'add_blackboard_key',
	'create_common_ui_widget',
	'list_common_ui_widgets',
	'get_world_partition_info',
	'load_world_partition_region',
	'list_pcg_graphs',
	'execute_pcg',
	'generate_ui_image',
	'generate_3d_model'
)

foreach ($expected in $expectedAliases) {
	if (-not $aliases.ContainsKey($expected)) {
		$errors.Add("Expected compatibility alias is missing: $expected")
	}
}

if ($errors.Count -gt 0) {
	Write-Host "Compatibility alias smoke failed." -ForegroundColor Red
	foreach ($err in $errors) {
		Write-Host " - $err" -ForegroundColor Red
	}
	exit 1
}

Write-Host "Compatibility alias smoke passed." -ForegroundColor Green
Write-Host ("Canonical tools: {0}" -f $toolNames.Count)
Write-Host ("Registered tool classes: {0}" -f $registeredClasses.Count)
Write-Host ("Compatibility aliases: {0}" -f $aliases.Count)
