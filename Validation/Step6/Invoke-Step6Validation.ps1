param(
    [string]$ProjectRoot = "G:\UEProjects\MyProject",
    [string]$ProjectFile = "G:\UEProjects\MyProject\MyProject.uproject",
    [string]$EditorExe = "G:\UE\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe",
    [string]$MapPath = "/Game/Maps/L_Empty",
    [string]$OutputRoot = "G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6",
    [int]$UEBridgePort = 8080,
    [int]$UnrealMcpPort = 13579,
    [switch]$LaunchEditor,
    [switch]$CloseEditorWhenDone,
    [int]$StartupTimeoutSeconds = 240,
    [int]$RequestTimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function ConvertTo-PrettyJson {
    param($Value)
    return $Value | ConvertTo-Json -Depth 100
}

function Write-JsonFile {
    param(
        [string]$Path,
        $Value
    )
    Ensure-Directory -Path (Split-Path -Parent $Path)
    [System.IO.File]::WriteAllText($Path, (ConvertTo-PrettyJson $Value), [System.Text.UTF8Encoding]::new($false))
}

function Write-TextFile {
    param(
        [string]$Path,
        [string]$Value
    )
    Ensure-Directory -Path (Split-Path -Parent $Path)
    [System.IO.File]::WriteAllText($Path, $Value, [System.Text.UTF8Encoding]::new($false))
}

function To-ArraySafe {
    param($Value)
    if ($null -eq $Value) {
        return @()
    }
    if (($Value -is [System.Collections.IEnumerable]) -and -not ($Value -is [string])) {
        return @($Value)
    }
    return @($Value)
}

function Wait-TcpPort {
    param(
        [string]$Address,
        [int]$Port,
        [int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $client = New-Object System.Net.Sockets.TcpClient
            $async = $client.BeginConnect($Address, $Port, $null, $null)
            if ($async.AsyncWaitHandle.WaitOne(1000, $false) -and $client.Connected) {
                $client.EndConnect($async) | Out-Null
                $client.Close()
                return $true
            }
            $client.Close()
        } catch {
        }
        Start-Sleep -Milliseconds 500
    }

    return $false
}

function Invoke-HttpJson {
    param(
        [string]$Uri,
        [string]$Method,
        $BodyObject,
        [hashtable]$Headers,
        [int]$TimeoutSeconds
    )

    $bodyJson = if ($null -ne $BodyObject) { ConvertTo-PrettyJson $BodyObject } else { $null }
    $handler = New-Object System.Net.Http.HttpClientHandler
    $client = New-Object System.Net.Http.HttpClient($handler)
    $client.Timeout = [TimeSpan]::FromSeconds($TimeoutSeconds)
    try {
        $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::new($Method), $Uri)
        foreach ($headerKey in $Headers.Keys) {
            $null = $request.Headers.TryAddWithoutValidation($headerKey, [string]$Headers[$headerKey])
        }
        if ($null -ne $bodyJson) {
            $request.Content = New-Object System.Net.Http.StringContent($bodyJson, [System.Text.Encoding]::UTF8, "application/json")
        }

        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $content = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $allHeaders = @{}
        foreach ($header in $response.Headers) {
            $allHeaders[$header.Key] = ($header.Value -join ", ")
        }
        foreach ($header in $response.Content.Headers) {
            $allHeaders[$header.Key] = ($header.Value -join ", ")
        }
        $parsed = $null
        if ($content) {
            $parsed = $content | ConvertFrom-Json
        }
        return [pscustomobject]@{
            Success = $response.IsSuccessStatusCode
            StatusCode = [int]$response.StatusCode
            Headers = $allHeaders
            Content = $content
            Json = $parsed
        }
    } catch {
        $statusCode = $null
        $headers = @{}
        $content = ""
        $parsed = $null
        if ($content) {
            try { $parsed = $content | ConvertFrom-Json } catch { }
        }
        return [pscustomobject]@{
            Success = $false
            StatusCode = $statusCode
            Headers = $headers
            Content = $content
            Json = $parsed
            Error = $_.Exception.Message
        }
    } finally {
        $client.Dispose()
        $handler.Dispose()
    }
}

function Invoke-McpCall {
    param(
        [hashtable]$Server,
        [string]$Method,
        $Params,
        [string]$Id
    )

    $headers = @{
        "Accept" = "application/json"
        "MCP-Protocol-Version" = $Server.ProtocolVersion
    }
    if ($Server.SessionId) {
        $headers["Mcp-Session-Id"] = $Server.SessionId
    }

    $body = @{
        jsonrpc = "2.0"
        method = $Method
    }
    if ($Id) {
        $body["id"] = $Id
    }
    if ($null -ne $Params) {
        $body["params"] = $Params
    }

    $response = Invoke-HttpJson -Uri $Server.Uri -Method "POST" -BodyObject $body -Headers $headers -TimeoutSeconds $Server.TimeoutSeconds
    if (($Method -eq "initialize") -and $response.Headers["Mcp-Session-Id"]) {
        $Server.SessionId = $response.Headers["Mcp-Session-Id"]
    }
    return [pscustomobject]@{
        Request = $body
        Response = $response
    }
}

function New-McpServerHandle {
    param(
        [string]$Name,
        [string]$Uri,
        [int]$TimeoutSeconds
    )

    $handle = @{
        Name = $Name
        Uri = $Uri
        SessionId = $null
        ProtocolVersion = "2025-06-18"
        TimeoutSeconds = $TimeoutSeconds
    }

    $initParams = @{
        protocolVersion = "2025-06-18"
        capabilities = @{}
        clientInfo = @{
            name = "UEBridgeMCP-Step6Validation"
            version = "1.0"
        }
    }

    $initialize = Invoke-McpCall -Server $handle -Method "initialize" -Params $initParams -Id "$Name-init"
    if (-not $initialize.Response.Success) {
        throw ("Failed to initialize {0} at {1}: {2}" -f $Name, $Uri, $initialize.Response.Error)
    }

    $null = Invoke-McpCall -Server $handle -Method "initialized" -Params @{} -Id $null
    return [pscustomobject]@{
        Handle = $handle
        Initialize = $initialize
    }
}

function Invoke-Tool {
    param(
        [hashtable]$Server,
        [string]$ToolName,
        $Arguments
    )

    $params = @{
        name = $ToolName
        arguments = if ($null -ne $Arguments) { $Arguments } else { @{} }
    }
    return Invoke-McpCall -Server $Server -Method "tools/call" -Params $params -Id ("tool-" + [System.Guid]::NewGuid().ToString("N"))
}

function Get-ToolDefinition {
    param(
        $ToolsListJson,
        [string]$ToolName
    )

    foreach ($tool in $ToolsListJson.result.tools) {
        if ($tool.name -eq $ToolName) {
            return $tool
        }
    }
    return $null
}

function Add-ScenarioResult {
    param(
        [System.Collections.Generic.List[object]]$Collection,
        [string]$Name,
        [string]$ServerName,
        $Invocation,
        [string]$Notes = ""
    )

    $Collection.Add([pscustomobject]@{
        scenario = $Name
        server = $ServerName
        success = [bool]$Invocation.Response.Success
        status_code = $Invocation.Response.StatusCode
        request = $Invocation.Request
        response = $Invocation.Response.Json
        raw_content = $Invocation.Response.Content
        notes = $Notes
    }) | Out-Null
}

function Get-ResultMessage {
    param($ToolResult)
    if ($ToolResult.Response.Json.result.structuredContent.message) {
        return [string]$ToolResult.Response.Json.result.structuredContent.message
    }
    if ($ToolResult.Response.Json.result.content[0].text) {
        return [string]$ToolResult.Response.Json.result.content[0].text
    }
    return ""
}

Ensure-Directory -Path $OutputRoot
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runRoot = Join-Path $OutputRoot $timestamp
Ensure-Directory -Path $runRoot

$summary = [ordered]@{
    started_at = (Get-Date).ToString("o")
    project_root = $ProjectRoot
    project_file = $ProjectFile
    map_path = $MapPath
    requested_ports = @{
        uebridge = $UEBridgePort
        unreal_mcp_server = $UnrealMcpPort
    }
    scenarios = @()
    observations = @()
}

$editorProcess = $null
if ($LaunchEditor) {
    $editorArgs = @(
        "`"$ProjectFile`"",
        $MapPath,
        "-NoSplash",
        "-log"
    )
    $editorProcess = Start-Process -FilePath $EditorExe -ArgumentList $editorArgs -PassThru
    $summary["editor_process"] = @{
        id = $editorProcess.Id
        exe = $EditorExe
    }
}

if (-not (Wait-TcpPort -Address "127.0.0.1" -Port $UEBridgePort -TimeoutSeconds $StartupTimeoutSeconds)) {
    throw "UEBridgeMCP endpoint did not become ready on port $UEBridgePort"
}
if (-not (Wait-TcpPort -Address "127.0.0.1" -Port $UnrealMcpPort -TimeoutSeconds $StartupTimeoutSeconds)) {
    throw "UnrealMCPServer endpoint did not become ready on port $UnrealMcpPort"
}

$ueBridgeServer = New-McpServerHandle -Name "uebridge" -Uri "http://127.0.0.1:$UEBridgePort/mcp" -TimeoutSeconds $RequestTimeoutSeconds
$unrealServer = New-McpServerHandle -Name "unrealmcp" -Uri "http://127.0.0.1:$UnrealMcpPort/mcp" -TimeoutSeconds $RequestTimeoutSeconds

$scenarioResults = New-Object 'System.Collections.Generic.List[object]'

$bridgeTools = Invoke-McpCall -Server $ueBridgeServer.Handle -Method "tools/list" -Params @{} -Id "uebridge-tools-list"
$unrealTools = Invoke-McpCall -Server $unrealServer.Handle -Method "tools/list" -Params @{} -Id "unrealmcp-tools-list"
$bridgeResources = Invoke-McpCall -Server $ueBridgeServer.Handle -Method "resources/list" -Params @{} -Id "uebridge-resources-list"
$unrealResources = Invoke-McpCall -Server $unrealServer.Handle -Method "resources/list" -Params @{} -Id "unrealmcp-resources-list"
$bridgePrompts = Invoke-McpCall -Server $ueBridgeServer.Handle -Method "prompts/list" -Params @{} -Id "uebridge-prompts-list"
$unrealPrompts = Invoke-McpCall -Server $unrealServer.Handle -Method "prompts/list" -Params @{} -Id "unrealmcp-prompts-list"

Add-ScenarioResult -Collection $scenarioResults -Name "protocol.initialize" -ServerName "UnrealMCPServer" -Invocation $unrealServer.Initialize
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.initialize" -ServerName "UEBridgeMCP" -Invocation $ueBridgeServer.Initialize
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.tools_list" -ServerName "UnrealMCPServer" -Invocation $unrealTools
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.tools_list" -ServerName "UEBridgeMCP" -Invocation $bridgeTools
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.resources_list" -ServerName "UnrealMCPServer" -Invocation $unrealResources
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.resources_list" -ServerName "UEBridgeMCP" -Invocation $bridgeResources
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.prompts_list" -ServerName "UnrealMCPServer" -Invocation $unrealPrompts
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.prompts_list" -ServerName "UEBridgeMCP" -Invocation $bridgePrompts

$bridgeResourceList = To-ArraySafe $bridgeResources.Response.Json.result.resources
$unrealResourceList = To-ArraySafe $unrealResources.Response.Json.result.resources
$bridgePromptList = To-ArraySafe $bridgePrompts.Response.Json.result.prompts
$unrealPromptList = To-ArraySafe $unrealPrompts.Response.Json.result.prompts

if ($unrealResourceList.Count -gt 0) {
    $firstUnrealUri = [string]$unrealResourceList[0].uri
    $unrealRead = Invoke-McpCall -Server $unrealServer.Handle -Method "resources/read" -Params @{ uri = $firstUnrealUri } -Id "unrealmcp-resources-read"
    Add-ScenarioResult -Collection $scenarioResults -Name "protocol.resources_read" -ServerName "UnrealMCPServer" -Invocation $unrealRead -Notes $firstUnrealUri
}

if ($bridgeResourceList.Count -gt 0) {
    $bridgeReadUri = [string](($bridgeResourceList | Where-Object { $_.uri -match 'performance|triage' } | Select-Object -First 1).uri)
    if (-not $bridgeReadUri) {
        $bridgeReadUri = [string]$bridgeResourceList[0].uri
    }
    $bridgeRead = Invoke-McpCall -Server $ueBridgeServer.Handle -Method "resources/read" -Params @{ uri = $bridgeReadUri } -Id "uebridge-resources-read"
    Add-ScenarioResult -Collection $scenarioResults -Name "protocol.resources_read" -ServerName "UEBridgeMCP" -Invocation $bridgeRead -Notes $bridgeReadUri
}

if ($unrealPromptList.Count -gt 0) {
    $firstPrompt = [string]$unrealPromptList[0].name
    $unrealPromptArgs = @{ goal = "Validate prompt expansion"; issue = "Baseline performance review"; world_mode = "editor" }
    $unrealPrompt = Invoke-McpCall -Server $unrealServer.Handle -Method "prompts/get" -Params @{ name = $firstPrompt; arguments = $unrealPromptArgs } -Id "unrealmcp-prompts-get"
    Add-ScenarioResult -Collection $scenarioResults -Name "protocol.prompts_get" -ServerName "UnrealMCPServer" -Invocation $unrealPrompt -Notes $firstPrompt
}

$bridgePrompt = Invoke-McpCall -Server $ueBridgeServer.Handle -Method "prompts/get" -Params @{
    name = "performance-triage-brief"
    arguments = @{
        goal = "Validate Step 6 performance workflow"
        world_mode = "editor"
        focus_area = "validation"
    }
} -Id "uebridge-prompts-get"
Add-ScenarioResult -Collection $scenarioResults -Name "protocol.prompts_get" -ServerName "UEBridgeMCP" -Invocation $bridgePrompt -Notes "performance-triage-brief"

$bridgeGetProjectInfo = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "get-project-info" -Arguments @{}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.project_info" -ServerName "UEBridgeMCP" -Invocation $bridgeGetProjectInfo

$performanceResource = [string](($bridgeResourceList | Where-Object { $_.uri -match 'performance' } | Select-Object -First 1).uri)
$presetDefinition = @{
    id = "step6-validation-performance"
    title = "Step6 Validation Performance"
    description = "Minimal preset used by the Step 6 validation harness."
    resource_uris = @($performanceResource)
    prompt_name = "performance-triage-brief"
    tool_calls = @(
        @{
            name = "query-performance-report"
            arguments = @{
                world = "editor"
            }
        }
    )
    default_arguments = @{
        goal = "Capture a lightweight editor performance baseline for Step 6 validation."
        world_mode = "editor"
        focus_area = "validation"
    }
}

$presetUpsert = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "manage-workflow-presets" -Arguments @{
    operations = @(
        @{
            action = "upsert_preset"
            preset = $presetDefinition
        }
    )
    dry_run = $false
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.preset_upsert" -ServerName "UEBridgeMCP" -Invocation $presetUpsert

$presetList = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "manage-workflow-presets" -Arguments @{
    operations = @(
        @{ action = "list_presets" }
    )
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.preset_list" -ServerName "UEBridgeMCP" -Invocation $presetList

$presetDryRun = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "run-workflow-preset" -Arguments @{
    preset_id = "step6-validation-performance"
    dry_run = $true
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.preset_run_dry" -ServerName "UEBridgeMCP" -Invocation $presetDryRun

$presetRun = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "run-workflow-preset" -Arguments @{
    preset_id = "step6-validation-performance"
    dry_run = $false
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.preset_run_live" -ServerName "UEBridgeMCP" -Invocation $presetRun

$performanceReport = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "query-performance-report" -Arguments @{
    world = "editor"
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.performance_report" -ServerName "UEBridgeMCP" -Invocation $performanceReport

$performanceSnapshot = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "capture-performance-snapshot" -Arguments @{
    world = "editor"
    include_viewport = $true
    include_logs = $true
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.performance_snapshot" -ServerName "UEBridgeMCP" -Invocation $performanceSnapshot

$toolNames = @(To-ArraySafe $bridgeTools.Response.Json.result.tools | ForEach-Object { $_.name })
$projectInfoContent = $bridgeGetProjectInfo.Response.Json.result.structuredContent
$optionalCapabilities = $projectInfoContent.optional_capabilities

if ($toolNames -contains "query-worldpartition-cells") {
    $worldPartitionQuery = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "query-worldpartition-cells" -Arguments @{
        world = "editor"
        include_content_bounds = $true
    }
    Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.worldpartition_negative" -ServerName "UEBridgeMCP" -Invocation $worldPartitionQuery
}

$duplicateAnimationFixtures = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "manage-assets" -Arguments @{
    actions = @(
        @{
            action = "duplicate"
            asset_path = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Idle.Tutorial_Idle"
            destination_path = "/Game/UEBridgeMCPValidation/Step6/Animations"
            new_name = "VLD_Tutorial_Idle"
        },
        @{
            action = "duplicate"
            asset_path = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_AnimBlueprint.TutorialTPP_AnimBlueprint"
            destination_path = "/Game/UEBridgeMCPValidation/Step6/Animations"
            new_name = "VLD_TutorialTPP_AnimBlueprint"
        }
    )
    save = $true
    dry_run = $false
    rollback_on_error = $true
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.animation_fixture_duplicate" -ServerName "UEBridgeMCP" -Invocation $duplicateAnimationFixtures

$validationIdlePath = "/Game/UEBridgeMCPValidation/Step6/Animations/VLD_Tutorial_Idle.VLD_Tutorial_Idle"
$validationAnimBpPath = "/Game/UEBridgeMCPValidation/Step6/Animations/VLD_TutorialTPP_AnimBlueprint.VLD_TutorialTPP_AnimBlueprint"
$validationMontagePath = "/Game/UEBridgeMCPValidation/Step6/Animations/VLD_TutorialMontage"

$queryIdle = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "query-animation-asset-summary" -Arguments @{
    asset_path = $validationIdlePath
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.animation_query_sequence" -ServerName "UEBridgeMCP" -Invocation $queryIdle

$queryAnimBp = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "query-animation-asset-summary" -Arguments @{
    asset_path = $validationAnimBpPath
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.animation_query_blueprint" -ServerName "UEBridgeMCP" -Invocation $queryAnimBp

$createMontage = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "create-animation-montage" -Arguments @{
    asset_path = $validationMontagePath
    sequence_paths = @($validationIdlePath)
    save = $true
    dry_run = $false
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.animation_create_montage" -ServerName "UEBridgeMCP" -Invocation $createMontage

$queryMontage = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "query-animation-asset-summary" -Arguments @{
    asset_path = "$validationMontagePath.VLD_TutorialMontage"
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.animation_query_montage" -ServerName "UEBridgeMCP" -Invocation $queryMontage

$stateMachineName = $null
$animBpStateMachines = To-ArraySafe $queryAnimBp.Response.Json.result.structuredContent.state_machines
if ($animBpStateMachines.Count -gt 0) {
    $stateMachineName = [string]$animBpStateMachines[0].name
}
if ($stateMachineName) {
    $editAnimStateMachine = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "edit-anim-blueprint-state-machine" -Arguments @{
        asset_path = $validationAnimBpPath
        state_machine_name = $stateMachineName
        operations = @(
            @{
                action = "add_state"
                state_name = "ValidationState"
                x = 640
                y = 96
            },
            @{
                action = "set_state_sequence"
                state_name = "ValidationState"
                sequence_path = $validationIdlePath
            }
        )
        save = $true
        compile = $true
        dry_run = $false
        rollback_on_error = $true
    }
    Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.animation_edit_state_machine" -ServerName "UEBridgeMCP" -Invocation $editAnimStateMachine -Notes $stateMachineName
}

$spawnSplineActor = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "edit-level-batch" -Arguments @{
    world = "editor"
    operations = @(
        @{
            action = "spawn_actor"
            actor_class = "Actor"
            actor_label = "VLD_SplineActor"
            transform = @{
                location = @(0, 0, 0)
                rotation = @(0, 0, 0)
                scale = @(1, 1, 1)
            }
        },
        @{
            action = "add_component"
            actor_name = "VLD_SplineActor"
            component_class = "SplineComponent"
            component_name = "ValidationSpline"
        }
    )
    save = $true
    dry_run = $false
    rollback_on_error = $true
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.spline_fixture_spawn" -ServerName "UEBridgeMCP" -Invocation $spawnSplineActor

$editSpline = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "edit-spline-actors" -Arguments @{
    world = "editor"
    operations = @(
        @{
            action = "set_point_transform"
            actor_name = "VLD_SplineActor"
            component_name = "ValidationSpline"
            index = 0
            position = @(0, 0, 0)
        },
        @{
            action = "add_point"
            actor_name = "VLD_SplineActor"
            component_name = "ValidationSpline"
            position = @(300, 0, 0)
        },
        @{
            action = "set_closed_loop"
            actor_name = "VLD_SplineActor"
            component_name = "ValidationSpline"
            closed_loop = $false
        }
    )
    save = $true
    dry_run = $false
    rollback_on_error = $true
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.spline_edit" -ServerName "UEBridgeMCP" -Invocation $editSpline

$externalAIToolPresent = $toolNames -contains "generate-external-content"
if ($externalAIToolPresent) {
    $externalAiUnconfigured = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "generate-external-content" -Arguments @{
        brief = "Produce a short validation summary as plain text."
        response_format = "text"
    }
    Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.external_ai_unconfigured" -ServerName "UEBridgeMCP" -Invocation $externalAiUnconfigured

    $externalAiBadConfig = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "generate-external-content" -Arguments @{
        brief = "Produce a short validation summary as JSON."
        response_format = "json"
        provider = "openai_compatible"
        api_base_url = "http://127.0.0.1:9/v1"
        api_key = "invalid-key-for-validation"
        model = "dummy-model"
        timeout_seconds = 2
    }
    Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.external_ai_bad_config" -ServerName "UEBridgeMCP" -Invocation $externalAiBadConfig
}

$presetDelete = Invoke-Tool -Server $ueBridgeServer.Handle -ToolName "manage-workflow-presets" -Arguments @{
    operations = @(
        @{
            action = "delete_preset"
            preset_id = "step6-validation-performance"
        }
    )
}
Add-ScenarioResult -Collection $scenarioResults -Name "uebridge.preset_delete" -ServerName "UEBridgeMCP" -Invocation $presetDelete

$bridgeCount = @($bridgeTools.Response.Json.result.tools).Count
$registeredCount = $ueBridgeServer.Initialize.Response.Json.result.capabilities.tools.registeredCount
$capabilityChecks = [ordered]@{
    registered_count = $registeredCount
    tools_list_count = $bridgeCount
    matches = ($registeredCount -eq $bridgeCount)
    tool_presence = @{
        edit_sequencer_tracks = ($toolNames -contains "edit-sequencer-tracks")
        edit_landscape_region = ($toolNames -contains "edit-landscape-region")
        edit_foliage_batch = ($toolNames -contains "edit-foliage-batch")
        query_worldpartition_cells = ($toolNames -contains "query-worldpartition-cells")
        edit_control_rig_graph = ($toolNames -contains "edit-control-rig-graph")
        generate_pcg_scatter = ($toolNames -contains "generate-pcg-scatter")
        generate_external_content = ($toolNames -contains "generate-external-content")
    }
    optional_capabilities = $optionalCapabilities
}

$scenarioResultsArray = @($scenarioResults | ForEach-Object { $_ })
$summary["ended_at"] = (Get-Date).ToString("o")
$summary["uebridge"] = @{
    session_id = $ueBridgeServer.Handle.SessionId
    initialize = $ueBridgeServer.Initialize.Response.Json
    tools_list_count = $bridgeCount
    resources_list_count = $bridgeResourceList.Count
    prompts_list_count = $bridgePromptList.Count
    capability_checks = $capabilityChecks
}
$summary["unreal_mcp_server"] = @{
    session_id = $unrealServer.Handle.SessionId
    initialize = $unrealServer.Initialize.Response.Json
    tools_list_count = @($unrealTools.Response.Json.result.tools).Count
    resources_list_count = $unrealResourceList.Count
    prompts_list_count = $unrealPromptList.Count
}
$summary["scenarios"] = $scenarioResultsArray

Write-JsonFile -Path (Join-Path $runRoot "summary.json") -Value $summary
Write-JsonFile -Path (Join-Path $runRoot "uebridge.initialize.json") -Value $ueBridgeServer.Initialize
Write-JsonFile -Path (Join-Path $runRoot "unrealmcp.initialize.json") -Value $unrealServer.Initialize
Write-JsonFile -Path (Join-Path $runRoot "uebridge.tools_list.json") -Value $bridgeTools
Write-JsonFile -Path (Join-Path $runRoot "unrealmcp.tools_list.json") -Value $unrealTools
Write-JsonFile -Path (Join-Path $runRoot "scenario-results.json") -Value $scenarioResultsArray

$closedEditor = $false
if ($CloseEditorWhenDone -and $editorProcess -and -not $editorProcess.HasExited) {
    try {
        $null = $editorProcess.CloseMainWindow()
        $closedEditor = $editorProcess.WaitForExit(60000)
    } catch {
    }
}

$summaryPath = Join-Path $runRoot "summary.json"
Write-Output "Step 6 validation artifacts written to: $runRoot"
Write-Output "Summary: $summaryPath"
if ($editorProcess) {
    Write-Output "Editor process id: $($editorProcess.Id)"
    Write-Output "Editor closed: $closedEditor"
}
