[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'
$powerShellPath = (Get-Process -Id $PID).Path

# Tests consume the supported non-executable bootstrap boundary. The executable
# entrypoint is exercised only as a child process.
. (Join-Path $repositoryRoot 'scripts\lib\Bootstrap.ps1') -RepositoryRoot $repositoryRoot

$helpOutput = (& $powerShellPath -NoProfile -ExecutionPolicy Bypass -File $helperPath help 2>&1 | Out-String)
foreach ($requiredHelpText in @('gamewip.bat <action> [command] [target]', 'Available commands', 'quality', 'tools', 'runs', '-Preview', '-NonInteractive', '-Yes', '-NoBuild', '-NoColor', '-Json'))
{
    if ($helpOutput -notmatch [regex]::Escape($requiredHelpText))
    {
        throw "Project helper help omits '$requiredHelpText'."
    }
}

Assert-GameWipCommandConfig
Assert-GameWipProjectToolConfig
if ((New-GameWipOperationContext -Label output-default).OutputMode -ne 'Stream')
{
    throw 'Project helper operations do not stream native output by default.'
}

$expectedSemanticColors = [ordered]@{
    Success = [ConsoleColor]::Green
    Failure = [ConsoleColor]::Red
    Warning = [ConsoleColor]::Yellow
    Accent = [ConsoleColor]::Cyan
    Muted = [ConsoleColor]::DarkGray
}
foreach ($semanticRole in $expectedSemanticColors.Keys)
{
    $actualColor = Get-GameWipSemanticColor -Semantic $semanticRole
    if ($actualColor -ne $expectedSemanticColors[$semanticRole])
    {
        throw "Semantic console role '$semanticRole' mapped to '$actualColor' instead of '$($expectedSemanticColors[$semanticRole])'."
    }
}
$expectedToolSemantics = [ordered]@{
    compatible = 'Success'
    missing = 'Failure'
    mismatch = 'Warning'
    outdated = 'Warning'
    unknown = 'Warning'
}
foreach ($compatibility in $expectedToolSemantics.Keys)
{
    $actualSemantic = Get-GameWipToolCompatibilitySemantic -Compatibility $compatibility
    if ($actualSemantic -ne $expectedToolSemantics[$compatibility])
    {
        throw "Tool compatibility '$compatibility' mapped to '$actualSemantic' instead of '$($expectedToolSemantics[$compatibility])'."
    }
}

$expectedMenuIds = @('root', 'development', 'validation', 'quality', 'tools', 'repository', 'maintenance')
$actualMenuIds = @($CommandConfig.Menus | ForEach-Object { [string]$_.Id })
if ((($expectedMenuIds | Sort-Object) -join "`n") -cne (($actualMenuIds | Sort-Object) -join "`n"))
{
    throw 'Project helper interactive menu topology is not fully declarative.'
}
$originalActionKeyReader = (Get-Command Read-GameWipActionKey).ScriptBlock
function Read-GameWipActionKey
{
    return New-GameWipChoiceResult -Status Selected -Value X
}
try
{
    $menuResult = Read-GameWipActionMenuItem `
        -Title 'Test menu' `
        -Prompt 'Choose:' `
        -Items @([pscustomobject]@{ Key = 'X'; Label = 'Test item'; Handler = 'test-handler' }) `
        -ExitLabel Back `
        6>$null
    if ($menuResult.Status -ne 'Selected' -or $menuResult.Value -ne 'test-handler')
    {
        throw 'The shared declarative menu renderer did not return the configured handler.'
    }
}
finally
{
    Set-Item -Path function:Read-GameWipActionKey -Value $originalActionKeyReader
}

$originalPresentationContext = $Script:OperationContext
try
{
    $Script:OperationContext = New-GameWipOperationContext -Label 'semantic-presentation-test'
    $coloredStatusOutput = (& {
            Write-GameWipStatusLine `
                -Status ready `
                -Text 'tool available' `
                -Suffix '(compatible)' `
                -Semantic Success `
                -SuffixSemantic Muted `
                -Indent 2 `
                -MarkerWidth 10
        } 6>&1 | ForEach-Object { [string]$_ }) -join ''

    $Script:OperationContext = New-GameWipOperationContext -Label 'plain-presentation-test' -NoColor
    $plainStatusOutput = (& {
            Write-GameWipStatusLine `
                -Status ready `
                -Text 'tool available' `
                -Suffix '(compatible)' `
                -Semantic Success `
                -SuffixSemantic Muted `
                -Indent 2 `
                -MarkerWidth 10
        } 6>&1 | ForEach-Object { [string]$_ }) -join ''

    $expectedStatusOutput = '  [ready]    tool available (compatible)'
    if ($coloredStatusOutput -ne $expectedStatusOutput)
    {
        throw "Semantic status rendering changed its text contract: '$coloredStatusOutput'."
    }
    if ($plainStatusOutput -ne $expectedStatusOutput)
    {
        throw "No-color status rendering changed its text contract: '$plainStatusOutput'."
    }
}
finally
{
    $Script:OperationContext = $originalPresentationContext
}

$schemaTestRoot = Join-Path $repositoryRoot ('build\gamewip\temp\schema-test-' + [guid]::NewGuid().ToString('N'))
try
{
    if ((Initialize-GameWipToolRun -RepositoryRoot $schemaTestRoot -RunLogRoot runs -Tool test -Action schema-check).SchemaVersion -ne 1)
    {
        throw 'Retained run receipts must remain at schema version 1.'
    }
}
finally
{
    if (Test-Path -LiteralPath $schemaTestRoot)
    {
        Remove-Item -LiteralPath $schemaTestRoot -Recurse -Force
    }
}
if ((Test-GameWipWindowsHost) -and (Get-GameWipExecutableNames -Command sample) -contains 'sample')
{
    throw 'Windows tool discovery included an extensionless shell entry point.'
}
$syntheticPythonRoot = if (Test-GameWipWindowsHost)
{
    'C:\gamewip-python-path-test'
}
else
{
    '/tmp/gamewip-python-path-test'
}
$expectedPythonPath = if (Test-GameWipWindowsHost)
{
    'C:\gamewip-python-path-test\Scripts\python.exe'
}
else
{
    '/tmp/gamewip-python-path-test/bin/python'
}
if ((Get-GameWipPythonEnvironmentInterpreterPath -Root $syntheticPythonRoot) -ne $expectedPythonPath)
{
    throw 'Python provider interpreter-path resolution is not platform-correct.'
}
$jsonSchemaTool = Get-GameWipProjectTool -Id 'jsonschema'
$detectedJsonSchema = Get-GameWipDetectedTool -Tool $jsonSchemaTool
if ((Get-GameWipToolCompatibility -Tool $jsonSchemaTool -Detected $detectedJsonSchema) -ne 'compatible')
{
    throw 'Python package discovery did not select an interpreter containing jsonschema.'
}

$actionIds = @($CommandConfig.Actions | ForEach-Object { [string]$_.Id })
if ($actionIds -contains 'analysis')
{
    throw "Retired 'analysis' alias is still public."
}
foreach ($requiredAction in @('menu', 'doctor', 'quality', 'tools', 'runs', 'analyze', 'help'))
{
    if ($actionIds -notcontains $requiredAction)
    {
        throw "Missing action metadata '$requiredAction'."
    }
}
$duplicateActions = @($actionIds | Group-Object | Where-Object Count -gt 1)
if ($duplicateActions.Count -ne 0)
{
    throw "Duplicate action IDs: $($duplicateActions.Name -join ', ')."
}

# Non-interactive mode suppresses prompts; it never grants mutation consent.
$Script:OperationContext = New-GameWipOperationContext -Label 'consent-test' -NonInteractive
$consentRejected = $false
try
{
    Confirm-GameWipMutation -Summary 'test mutation' -Risk machine -Plan @('test') | Out-Null
}
catch
{
    $consentRejected = $_.Exception.Data['GameWipCode'] -eq 'consent-required'
}
if (-not $consentRejected)
{
    throw '-NonInteractive implicitly authorized a machine mutation.'
}

$Script:OperationContext = New-GameWipOperationContext -Label 'preview-test' -Preview
if (Confirm-GameWipMutation -Summary 'preview mutation' -Risk tracked -Plan @('test'))
{
    throw '-Preview authorized a tracked mutation.'
}
$Script:OperationContext = New-GameWipOperationContext -Label 'yes-test' -NonInteractive -Yes
if (-not (Confirm-GameWipMutation -Summary 'approved mutation' -Risk machine -Plan @('test')))
{
    throw '-Yes did not provide explicit non-interactive consent.'
}
$Script:OperationContext = $null

# Internal console assertions must never trigger PowerShell parameter binding
# prompts when a caller omits descriptive metadata.
$interactiveConsoleImplementation = (Get-Command Test-GameWipInteractiveConsole).ScriptBlock
function Test-GameWipInteractiveConsole
{
    return $false
}
$interactiveRejected = $false
try
{
    try
    {
        Assert-GameWipInteractiveConsole
    }
    catch
    {
        $interactiveRejected = $_.Exception.Data['GameWipCode'] -eq 'interactive-console-required'
    }
}
finally
{
    Set-Item -Path function:Test-GameWipInteractiveConsole -Value $interactiveConsoleImplementation
}
if (-not $interactiveRejected)
{
    throw 'A non-interactive console assertion did not fail with the supported diagnostic.'
}

# Incidental operation-body output must not turn the final result into an
# array or hide its Status property.
$originalRunRoot = [string]$ProjectConfig.storage.runs
$resultShapeRoot = 'build/gamewip/temp/result-shape-test-' + [guid]::NewGuid().ToString('N')
try
{
    $ProjectConfig.storage.runs = "$resultShapeRoot/runs"
    $operationResult = Invoke-GameWipOperation -Label 'result-shape-test' -SuppressReceipt -ScriptBlock {
        [pscustomobject]@{ Incidental = $true }
        throw 'expected result-shape failure'
    }
}
finally
{
    $ProjectConfig.storage.runs = $originalRunRoot
    $resultShapePath = Join-Path $repositoryRoot $resultShapeRoot
    if (Test-Path -LiteralPath $resultShapePath)
    {
        Remove-Item -LiteralPath $resultShapePath -Recurse -Force
    }
}
if ($operationResult -is [array] -or $operationResult.Status -ne 'failed')
{
    throw 'Operation failure did not return exactly one structured result.'
}

# Catalogs larger than the single-key alphabet fall back to numbered input.
$interactiveConsoleImplementation = (Get-Command Test-GameWipInteractiveConsole).ScriptBlock
function Test-GameWipInteractiveConsole
{
    return $true
}
function Read-Host
{
    return '25'
}
try
{
    $largeChoices = @(1..25 | ForEach-Object { "choice-$_" })
    $largeChoice = Read-GameWipMenuChoiceResult -Prompt 'Large catalog' -Choices $largeChoices -Default 'choice-1'
    if ($largeChoice.Status -ne 'Selected' -or $largeChoice.Value -ne 'choice-25')
    {
        throw 'A large menu catalog did not accept numbered input.'
    }
}
finally
{
    Remove-Item -Path function:Read-Host
    Set-Item -Path function:Test-GameWipInteractiveConsole -Value $interactiveConsoleImplementation
}

# Repository text persistence is strict UTF-8 without BOM and can atomically
# replace an existing file without losing non-ASCII content.
$atomicRoot = Join-Path $repositoryRoot ('build\gamewip\temp\atomic-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $atomicRoot | Out-Null
try
{
    $atomicPath = Join-Path $atomicRoot 'state.txt'
    $unicodeText = (
        'GameWIP ' +
        [char]0x2014 +
        ' UTF-8: ' +
        [char]0x0103 +
        [char]0x00EE +
        [char]0x0219 +
        [char]0x021B +
        ' ' +
        [char]0x20AC
    )

    Write-GameWipTextAtomic -Path $atomicPath -Content 'first'
    Write-GameWipTextAtomic -Path $atomicPath -Content $unicodeText

    $readBack = Read-GameWipUtf8Text -Path $atomicPath
    if ($readBack -cne $unicodeText)
    {
        throw 'UTF-8 persistence did not preserve repository text exactly.'
    }

    $bytes = [IO.File]::ReadAllBytes($atomicPath)
    if (
        $bytes.Length -ge 3 -and
        $bytes[0] -eq 0xEF -and
        $bytes[1] -eq 0xBB -and
        $bytes[2] -eq 0xBF
    )
    {
        throw 'Repository text persistence introduced a UTF-8 BOM.'
    }

    $invalidUtf8Path = Join-Path $atomicRoot 'invalid-utf8.txt'
    [IO.File]::WriteAllBytes(
        $invalidUtf8Path,
        [byte[]](0xC3, 0x28)
    )

    $invalidUtf8Rejected = $false
    try
    {
        $null = Read-GameWipUtf8Text -Path $invalidUtf8Path
    }
    catch [System.Text.DecoderFallbackException]
    {
        $invalidUtf8Rejected = $true
    }

    if (-not $invalidUtf8Rejected)
    {
        throw 'Repository UTF-8 reads silently accepted malformed UTF-8.'
    }
}
finally
{
    if (Test-Path -LiteralPath $atomicRoot)
    {
        Remove-Item -LiteralPath $atomicRoot -Recurse -Force
    }
}

# A single suggested action remains a collection under strict mode.
$diagnostic = New-GameWipDiagnosticException -Code test-diagnostic -Summary 'expected failure' -SuggestedActions @('single recovery action')
$failureOutput = @(& { Show-GameWipActionFailure -ErrorRecord ([System.Management.Automation.ErrorRecord]::new($diagnostic, 'test', 'NotSpecified', $null)) } *>&1) -join "`n"
if ($failureOutput -notmatch 'single recovery action')
{
    throw 'Failure rendering lost a scalar suggested action.'
}

# Operation identity must be fresh for each action selection.
$first = New-GameWipOperationContext -Label 'first'
$second = New-GameWipOperationContext -Label 'second'
if ($first.Id -eq $second.Id)
{
    throw 'Operation contexts reused an identity.'
}
if ($first.MutationState -ne 'none' -or $second.MutationState -ne 'none')
{
    throw 'A fresh operation inherited mutation state.'
}

# Candidate ranking is explicit rather than dependent on filesystem enumeration.
$candidates = [System.Collections.Generic.List[object]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$candidateRoot = Join-Path $repositoryRoot ('build\gamewip\temp\candidate-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $candidateRoot | Out-Null
try
{
    $preferred = Join-Path $candidateRoot 'preferred.exe'
    $fallback = Join-Path $candidateRoot 'fallback.exe'
    New-Item -ItemType File -Path $preferred, $fallback | Out-Null
    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path $fallback -Source PATH -Priority 100 -SelectionReason 'PATH fallback'
    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path $preferred -Source managed -Priority 20 -SelectionReason 'declared provider location'
    $ordered = @($candidates | Sort-Object Priority, Path)
    if ($ordered[0].Path -ne [IO.Path]::GetFullPath($preferred) -or $ordered[0].SelectionReason -ne 'declared provider location')
    {
        throw 'Tool-candidate ranking is not deterministic.'
    }
}
finally
{
    if (Test-Path -LiteralPath $candidateRoot)
    {
        Remove-Item -LiteralPath $candidateRoot -Recurse -Force
    }
}

# Windows command shims are valid managed-tool entry points and must be launched
# through the command interpreter rather than passed directly to Start-Process.
if (Test-GameWipWindowsHost)
{
    $shimRoot = Join-Path $repositoryRoot ('build\gamewip\temp\process-shim-test-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $shimRoot | Out-Null
    try
    {
        $shim = Join-Path $shimRoot 'sample.cmd'
        [IO.File]::WriteAllText($shim, "@echo off`r`necho shim-ok`r`n")
        $shimResult = Invoke-GameWipProcess -FilePath $shim -OutputMode LogOnly -TimeoutSeconds 10
        if ($shimResult.ExitCode -ne 0 -or (@($shimResult.Stdout) -join "`n") -notmatch 'shim-ok')
        {
            throw 'Windows command shim execution failed.'
        }
        $failedShim = Join-Path $shimRoot 'failure.cmd'
        [IO.File]::WriteAllText($failedShim, "@echo off`r`nexit /b 7`r`n")
        $failedShimResult = Invoke-GameWipProcess -FilePath $failedShim -OutputMode LogOnly -TimeoutSeconds 10
        if ($failedShimResult.ExitCode -ne 7)
        {
            throw "Windows command shim exit code was reported as $($failedShimResult.ExitCode), not 7."
        }
    }
    finally
    {
        if (Test-Path -LiteralPath $shimRoot)
        {
            Remove-Item -LiteralPath $shimRoot -Recurse -Force
        }
    }
}

# The retained run document is a structured operation receipt.
$runRoot = Join-Path $repositoryRoot ('build\gamewip\temp\run-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
try
{
    $run = Initialize-GameWipToolRun -RepositoryRoot $runRoot -RunLogRoot 'runs' -Tool 'test' -Action 'sample'
    $firstStep = Initialize-GameWipToolRunStep -Run $run -Name first -CommandLine 'first command'
    $secondStep = Initialize-GameWipToolRunStep -Run $run -Name second -CommandLine 'second command'
    if ($firstStep.Index -ne 1 -or $secondStep.Index -ne 2 -or $firstStep.LogPath -eq $secondStep.LogPath)
    {
        throw 'Overlapping retained steps did not reserve unique indices and log paths.'
    }
    Complete-GameWipToolRunStep -Run $run -Step $secondStep -ExitCode 0
    Complete-GameWipToolRunStep -Run $run -Step $firstStep -ExitCode 0
    $run.MutationState = 'complete'
    $run.Changes = @('changed sample')
    $summary = Save-GameWipToolRun -Run $run -Status passed
    $document = Get-Content -Raw -LiteralPath (Join-Path $run.Root 'summary.json') | ConvertFrom-Json
    if ($document.status -ne 'passed' -or $document.mutationState -ne 'complete' -or @($document.changes).Count -ne 1)
    {
        throw 'Structured run receipt lost operation result fields.'
    }
    if (-not (Test-Path -LiteralPath $summary))
    {
        throw 'Human-readable run summary was not created.'
    }
}
finally
{
    if (Test-Path -LiteralPath $runRoot)
    {
        Remove-Item -LiteralPath $runRoot -Recurse -Force
    }
}

if (-not (Test-GameWipWindowsHost))
{
    $ownedProcess = $null
    $descendantIds = @()
    $unixCancellationRoot = Join-Path $repositoryRoot ('build/gamewip/temp/unix-cancellation-test-' + [guid]::NewGuid().ToString('N'))
    try
    {
        New-Item -ItemType Directory -Path $unixCancellationRoot -Force | Out-Null
        $parentScript = Join-Path $unixCancellationRoot 'parent.sh'
        $childProcessIdPath = Join-Path $unixCancellationRoot 'child.pid'
        $parentScriptContent = @('#!/bin/sh', 'child_pid_file=$1', 'sleep 60 &', 'echo $! > "$child_pid_file"', 'wait', '') -join "`n"
        [IO.File]::WriteAllText($parentScript, $parentScriptContent, [Text.UTF8Encoding]::new($false))
        $ownedProcess = Start-GameWipOwnedProcess -FilePath '/bin/sh' -Arguments @($parentScript, $childProcessIdPath)
        if (-not [bool]$ownedProcess.GameWipOwnProcessGroup)
        {
            throw 'Unix owned-process launch did not establish an isolated process group.'
        }
        $discoveryDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do
        {
            Start-Sleep -Milliseconds 100
            if (Test-Path -LiteralPath $childProcessIdPath -PathType Leaf)
            {
                $childProcessId = 0
                $childProcessIdText = Get-Content -Raw -LiteralPath $childProcessIdPath
                if ([int]::TryParse($childProcessIdText.Trim(), [ref]$childProcessId) -and $childProcessId -gt 0)
                {
                    $descendantIds = @($childProcessId)
                }
            }
        }
        while ($descendantIds.Count -eq 0 -and -not $ownedProcess.HasExited -and [DateTime]::UtcNow -lt $discoveryDeadline)
        if ($descendantIds.Count -eq 0)
        {
            throw 'Unix cancellation regression could not observe the owned child process.'
        }
        Stop-GameWipOwnedProcess -Process $ownedProcess
        $null = $ownedProcess.WaitForExit(5000)
        foreach ($descendantId in $descendantIds)
        {
            $terminationDeadline = [DateTime]::UtcNow.AddSeconds(5)
            while ($null -ne (Get-Process -Id $descendantId -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $terminationDeadline)
            {
                Start-Sleep -Milliseconds 100
            }
            if ($null -ne (Get-Process -Id $descendantId -ErrorAction SilentlyContinue))
            {
                throw "Unix cancellation left descendant process $descendantId running."
            }
        }
        $previousLastExitCode = $global:LASTEXITCODE
        try
        {
            $global:LASTEXITCODE = 73
            Stop-GameWipOwnedProcess -Process $ownedProcess
            if ($global:LASTEXITCODE -ne 73)
            {
                throw 'Owned-process cleanup leaked a native process exit code.'
            }
        }
        finally
        {
            $global:LASTEXITCODE = $previousLastExitCode
        }
    }
    finally
    {
        if ($null -ne $ownedProcess)
        {
            Stop-GameWipOwnedProcess -Process $ownedProcess
        }
        foreach ($descendantId in $descendantIds)
        {
            Stop-Process -Id $descendantId -Force -ErrorAction SilentlyContinue
        }
        Remove-Item -LiteralPath $unixCancellationRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$hashRoot = Join-Path $repositoryRoot ('build\gamewip\temp\hash-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $hashRoot | Out-Null
try
{
    $hashPath = Join-Path $hashRoot 'sample.txt'
    [IO.File]::WriteAllText($hashPath, 'first', [Text.UTF8Encoding]::new($false))
    $firstHash = Get-GameWipFileSha256 -Path $hashPath
    if ($firstHash -notmatch '^[0-9a-f]{64}$' -or $firstHash -ne (Get-GameWipFileSha256 -Path $hashPath))
    {
        throw 'Repository-owned SHA-256 hashing is not stable.'
    }
    [IO.File]::WriteAllText($hashPath, 'second', [Text.UTF8Encoding]::new($false))
    if ($firstHash -eq (Get-GameWipFileSha256 -Path $hashPath))
    {
        throw 'Repository-owned SHA-256 hashing did not detect a content change.'
    }
}
finally
{
    Remove-Item -LiteralPath $hashRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$Script:OperationContext = New-GameWipOperationContext -Label 'mutation-state-before-write'
try
{
    try
    {
        Invoke-GameWipMutation -Summary 'fail before mutation' -Risk local -Plan @('fail first') -Body { throw 'expected pre-mutation failure' } | Out-Null
    }
    catch
    {
        $null = $_.Exception
    }
    if ($Script:OperationContext.MutationState -ne 'none')
    {
        throw 'A failure before the first mutating step was incorrectly reported as partial mutation.'
    }
}
finally
{
    $Script:OperationContext = $null
}

$managedRootImplementation = (Get-Command Get-GameWipManagedToolRoot).ScriptBlock
$managedRootTest = Join-Path $repositoryRoot ('build\gamewip\temp\managed-root-test-' + [guid]::NewGuid().ToString('N'))
function Get-GameWipManagedToolRoot
{
    return $managedRootTest
}
$Script:OperationContext = New-GameWipOperationContext -Label 'managed-root-idempotence'
$Script:OperationContext.MutationIntent = $true
try
{
    Initialize-GameWipManagedToolRoot
    if ($Script:OperationContext.MutationState -ne 'partial')
    {
        throw 'Creating the managed tool root did not mark action mutation.'
    }
    $markerPath = Join-Path $managedRootTest '.gamewip-managed.json'
    $markerBefore = Get-Content -Raw -LiteralPath $markerPath
    $Script:OperationContext.MutationState = 'none'
    Initialize-GameWipManagedToolRoot
    if ($Script:OperationContext.MutationState -ne 'none' -or (Get-Content -Raw -LiteralPath $markerPath) -ne $markerBefore)
    {
        throw 'Reinitializing a complete managed tool root rewrote persistent state.'
    }
}
finally
{
    $Script:OperationContext = $null
    Set-Item -Path function:Get-GameWipManagedToolRoot -Value $managedRootImplementation
    Remove-Item -LiteralPath $managedRootTest -Recurse -Force -ErrorAction SilentlyContinue
}

$managedRootImplementation = (Get-Command Get-GameWipManagedToolRoot).ScriptBlock
$downloadImplementation = (Get-Command Invoke-GameWipDownload).ScriptBlock
$hashImplementation = (Get-Command Get-GameWipFileSha256).ScriptBlock
$candidateVersionImplementation = (Get-Command Get-GameWipToolCandidateVersion).ScriptBlock
$shimImplementation = (Get-Command Write-GameWipManagedToolShim).ScriptBlock
$releaseRollbackRoot = Join-Path $repositoryRoot ('build\gamewip\temp\release-rollback-test-' + [guid]::NewGuid().ToString('N'))
$releaseManagedRoot = Join-Path $releaseRollbackRoot 'managed'
$releaseOperationTemp = Join-Path $releaseRollbackRoot 'operation'
function Get-GameWipManagedToolRoot
{
    return $releaseManagedRoot
}
function Invoke-GameWipDownload
{
    param([string]$Uri, [string]$OutFile, [string]$Label)
    $null = $Uri, $Label
    [IO.File]::WriteAllText($OutFile, 'candidate', [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ State = 'resolved'; Reason = '' }
}
function Get-GameWipFileSha256
{
    param([string]$Path)
    $null = $Path
    return 'expected-hash'
}
function Get-GameWipToolCandidateVersion
{
    param([hashtable]$Tool, [string]$Path)
    $null = $Tool, $Path
    return '1.0.0'
}
function Write-GameWipManagedToolShim
{
    param([string]$ToolId, [string]$Version, [string]$ExecutableName)
    $null = $Version, $ExecutableName
    $shimName = if (Test-GameWipWindowsHost)
    {
        "$ToolId.cmd"
    }
    else
    {
        $ToolId
    }
    Write-GameWipTextAtomic -Path (Join-Path (Join-Path $releaseManagedRoot 'bin') $shimName) -Content 'new shim'
    throw 'expected shim replacement failure'
}
$Script:OperationContext = New-GameWipOperationContext -Label 'release-rollback'
$Script:OperationContext.MutationIntent = $true
$Script:OperationContext.Temp = $releaseOperationTemp
try
{
    New-Item -ItemType Directory -Path $releaseOperationTemp -Force | Out-Null
    Initialize-GameWipManagedToolRoot
    $oldToolRoot = Join-Path $releaseManagedRoot 'tools/release-test/1.0.0'
    New-Item -ItemType Directory -Path $oldToolRoot -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $oldToolRoot 'old.txt'), 'old tool', [Text.UTF8Encoding]::new($false))
    $shimName = if (Test-GameWipWindowsHost)
    {
        'release-test.cmd'
    }
    else
    {
        'release-test'
    }
    $oldShim = Join-Path (Join-Path $releaseManagedRoot 'bin') $shimName
    [IO.File]::WriteAllText($oldShim, 'old shim', [Text.UTF8Encoding]::new($false))
    $releaseTool = @{
        id = 'release-test'
        detection = @{ versionArguments = @('--version'); versionPattern = '([0-9.]+)' }
        provider = @{
            repository = 'example/example'
            releaseTag = 'v1.0.0'
            assets = @{
                'windows-amd64' = @{ archive = 'release-test.exe'; format = 'executable'; sha256 = 'expected-hash' }
                'linux-amd64' = @{ archive = 'release-test'; format = 'executable'; sha256 = 'expected-hash' }
            }
        }
    }
    $replacementFailed = $false
    try
    {
        Install-GameWipGitHubReleaseTool -Tool $releaseTool -Version '1.0.0'
    }
    catch
    {
        $replacementFailed = $_.Exception.Message -eq 'expected shim replacement failure'
    }
    if (-not $replacementFailed -or
        (Get-Content -Raw -LiteralPath (Join-Path $oldToolRoot 'old.txt')) -ne 'old tool' -or
        (Get-Content -Raw -LiteralPath $oldShim) -ne 'old shim' -or
        @(Get-ChildItem -LiteralPath (Join-Path $releaseManagedRoot 'tools') -Force -Recurse | Where-Object Name -match '\.(?:incoming|backup)$').Count -ne 0)
    {
        throw 'A failed GitHub-release replacement did not restore the prior tool and shim exactly.'
    }
}
finally
{
    $Script:OperationContext = $null
    Set-Item -Path function:Get-GameWipManagedToolRoot -Value $managedRootImplementation
    Set-Item -Path function:Invoke-GameWipDownload -Value $downloadImplementation
    Set-Item -Path function:Get-GameWipFileSha256 -Value $hashImplementation
    Set-Item -Path function:Get-GameWipToolCandidateVersion -Value $candidateVersionImplementation
    Set-Item -Path function:Write-GameWipManagedToolShim -Value $shimImplementation
    Remove-Item -LiteralPath $releaseRollbackRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$processLogRoot = Join-Path $repositoryRoot ('build\gamewip\temp\process-log-mutation-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $processLogRoot | Out-Null

$Script:OperationContext = New-GameWipOperationContext -Label 'process-log-does-not-count-as-mutation'
$Script:OperationContext.MutationIntent = $true
$Script:OperationContext.Temp = $processLogRoot

try
{
    $processResult = Invoke-GameWipProcess `
        -FilePath 'git' `
        -Arguments @('--version') `
        -OutputMode LogOnly `
        -LogPath (Join-Path $processLogRoot 'git-version.log')

    if ($processResult.ExitCode -ne 0)
    {
        throw 'Read-only native query failed during mutation-state regression coverage.'
    }

    if ($Script:OperationContext.MutationState -ne 'none')
    {
        throw 'Retained native-process logging was incorrectly counted as action mutation.'
    }
}
finally
{
    $Script:OperationContext = $null
    Remove-Item -LiteralPath $processLogRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$savedLastExitCode = Get-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
$exitedProcess = Start-Process -FilePath $powerShellPath -ArgumentList @('-NoProfile', '-Command', 'exit 0') -PassThru
$exitedProcess.WaitForExit()
$exitedProcess | Add-Member -NotePropertyName GameWipOwnProcessGroup -NotePropertyValue $false
try
{
    Remove-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
    Stop-GameWipOwnedProcess -Process $exitedProcess
}
finally
{
    if ($null -ne $savedLastExitCode)
    {
        $global:LASTEXITCODE = $savedLastExitCode.Value
    }
}

$atomicRoot = Join-Path $repositoryRoot (
    'build\gamewip\temp\atomic-parent-mutation-test-' +
    [guid]::NewGuid().ToString('N')
)

$Script:OperationContext = New-GameWipOperationContext -Label 'atomic-parent-counts-as-mutation'
$Script:OperationContext.MutationIntent = $true

try
{
    Write-GameWipTextAtomic `
        -Path (Join-Path $atomicRoot 'nested\result.txt') `
        -Content 'test'

    if ($Script:OperationContext.MutationState -ne 'partial')
    {
        throw 'Creating persistent atomic-write state did not mark action mutation.'
    }
}
finally
{
    $Script:OperationContext = $null
    Remove-Item -LiteralPath $atomicRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# Tracked tool planning is declarative, composes same-file references, and
# stages source-preserving compare-and-set mutations without provider/network calls.
$trackedPlanRoot = Join-Path $repositoryRoot ('build\gamewip\temp\tracked-plan-test-' + [guid]::NewGuid().ToString('N'))
$savedRepositoryRoot = $Script:RepositoryRoot
$savedProjectToolsPath = $Script:ProjectToolsPath
$savedProjectTools = $Script:ProjectTools
$savedOperationContext = $Script:OperationContext
try
{
    New-Item -ItemType Directory -Path $trackedPlanRoot -Force | Out-Null
    $sharedReferencePath = Join-Path $trackedPlanRoot 'shared.md'
    $historicalReferencePath = Join-Path $trackedPlanRoot 'historical.md'
    $emDash = [char]0x2014
    $romanianText = [string]([char]0x0103) + [char]0x00EE + [char]0x0219 + [char]0x021B
    $sharedSource = "alpha=1.0.0`nbeta=2.0.0`nunrelated=$emDash $romanianText`n"
    [IO.File]::WriteAllText($sharedReferencePath, $sharedSource, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($historicalReferencePath, 'historical=3.0.0', [Text.UTF8Encoding]::new($false))

    $commonDetection = @{ command = 'test'; versionArguments = @('--version'); versionPattern = '([0-9.]+)' }
    $commonCapabilities = @{ detectInstalled = $true; checkLatest = $true; update = $true }
    $testRegistry = @{
        schemaVersion = 1
        tools = @(
            @{
                id = 'alpha'; name = 'Alpha'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '1.0.0'
                provider = @{ kind = 'npm'; package = 'alpha'; dependencies = @(@{ package = 'alpha-dependency'; version = '4.0.0' }) }
                detection = Copy-GameWipValue -Value $commonDetection
                capabilities = Copy-GameWipValue -Value $commonCapabilities
                references = @(@{ path = 'shared.md'; kind = 'text'; pattern = 'alpha={version}'; expectedCount = 1 })
            },
            @{
                id = 'beta'; name = 'Beta'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '2.0.0'
                provider = @{ kind = 'npm'; package = 'beta' }
                detection = Copy-GameWipValue -Value $commonDetection
                capabilities = Copy-GameWipValue -Value $commonCapabilities
                references = @(@{ path = 'shared.md'; kind = 'text'; pattern = 'beta={version}'; expectedCount = 1 })
            },
            @{
                id = 'historical'; name = 'Historical'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '3.0.0'
                provider = @{ kind = 'npm'; package = 'historical' }
                detection = Copy-GameWipValue -Value $commonDetection
                capabilities = Copy-GameWipValue -Value $commonCapabilities
                references = @(@{ path = 'historical.md'; kind = 'path' })
            },
            @{
                id = 'release'; name = 'Release'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '5.0.0'
                provider = @{
                    kind = 'githubRelease'; repository = 'example/release'; releaseTag = 'v5.0.0'
                    assets = @{
                        'windows-amd64' = @{ archive = 'release_5.0.0_windows.zip'; sha256 = ('a' * 64) }
                        'linux-amd64' = @{ archive = 'release_5.0.0_linux.tar.gz'; sha256 = ('b' * 64) }
                    }
                }
                detection = Copy-GameWipValue -Value $commonDetection
                capabilities = Copy-GameWipValue -Value $commonCapabilities
                references = @()
            }
        )
    }
    $testRegistryPath = Join-Path $trackedPlanRoot 'project-tools.json'
    [IO.File]::WriteAllText($testRegistryPath, (($testRegistry | ConvertTo-Json -Depth 20) + "`n"), [Text.UTF8Encoding]::new($false))
    $Script:RepositoryRoot = $trackedPlanRoot
    $Script:ProjectToolsPath = $testRegistryPath
    $Script:ProjectTools = $testRegistry
    $Script:OperationContext = New-GameWipOperationContext -Label 'tracked-plan-test'
    $Script:OperationContext.Temp = $trackedPlanRoot

    $plan = @(
        [pscustomobject]@{
            Tool = $testRegistry.tools[0]; Latest = '1.1.0'; LatestDependencies = @{ 'alpha-dependency' = '4.1.0' }
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ReleaseMetadata = $null
        },
        [pscustomobject]@{
            Tool = $testRegistry.tools[1]; Latest = '2.1.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ReleaseMetadata = $null
        },
        [pscustomobject]@{
            Tool = $testRegistry.tools[2]; Latest = '3.1.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ReleaseMetadata = $null
        },
        [pscustomobject]@{
            Tool = $testRegistry.tools[3]; Latest = '5.1.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }
            ReleaseMetadata = @{
                Tag = 'v5.1.0'
                Assets = @{
                    'windows-amd64' = @{ archive = 'release_5.1.0_windows.zip'; sha256 = ('c' * 64) }
                    'linux-amd64' = @{ archive = 'release_5.1.0_linux.tar.gz'; sha256 = ('d' * 64) }
                }
            }
        }
    )
    $trackedPlan = Get-GameWipTrackedToolMutationPlan -Plan $plan
    $sharedStaged = [string]$trackedPlan.Files[$sharedReferencePath]
    if ($sharedStaged -cne "alpha=1.1.0`nbeta=2.1.0`nunrelated=$emDash $romanianText`n")
    {
        throw 'Multiple live reference mutations did not compose while preserving unrelated UTF-8 text.'
    }
    if ($trackedPlan.Files.ContainsKey($historicalReferencePath))
    {
        throw 'An informational path reference was automatically mutated.'
    }
    $mutationTargets = @($trackedPlan.RegistryMutations | ForEach-Object {
            [string]$_.toolId + ':' + (@($_.path | ForEach-Object {
                        if ($_ -is [hashtable])
                        {
                            '[alpha-dependency]'
                        }
                        else
                        {
                            [string]$_
                        }
                    }) -join '.')
        })
    foreach ($expectedTarget in @(
            'alpha:requiredVersion',
            'alpha:provider.dependencies.[alpha-dependency].version',
            'release:requiredVersion',
            'release:provider.releaseTag',
            'release:provider.assets.windows-amd64.archive',
            'release:provider.assets.windows-amd64.sha256'
        ))
    {
        if ($mutationTargets -notcontains $expectedTarget)
        {
            throw "Tracked mutation planning omitted '$expectedTarget'."
        }
    }
    $stagedRegistrySource = [string]$trackedPlan.Files[$testRegistryPath]
    if ($stagedRegistrySource -notmatch '"requiredVersion":\s+"1\.1\.0"' -or $stagedRegistrySource -notmatch '"version":\s+"4\.1\.0"')
    {
        throw 'Structured registry mutations did not stage planned exact/dependency versions.'
    }

    $badReferenceTool = Copy-GameWipValue -Value $testRegistry.tools[0]
    $badReferenceTool.references[0].expectedCount = 2
    $countRejected = $false
    try
    {
        Get-GameWipTrackedToolMutationPlan -Plan @([pscustomobject]@{
                Tool = $badReferenceTool; Latest = '1.1.0'; LatestDependencies = @{}
                Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ReleaseMetadata = $null
            }) | Out-Null
    }
    catch
    {
        $countRejected = $_.Exception.Message -like '*expected 2 exact match(es), found 1*'
    }
    if (-not $countRejected)
    {
        throw 'Live reference expectedCount mismatch was not rejected.'
    }

    $noOpPlan = Get-GameWipTrackedToolMutationPlan -Plan @([pscustomobject]@{
            Tool = $testRegistry.tools[1]; Latest = '2.0.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ReleaseMetadata = $null
        })
    if ($noOpPlan.RegistryMutations.Count -ne 0 -or $noOpPlan.Files.Count -ne 0)
    {
        throw 'No-op tracked planning staged repository changes.'
    }
}
finally
{
    $Script:RepositoryRoot = $savedRepositoryRoot
    $Script:ProjectToolsPath = $savedProjectToolsPath
    $Script:ProjectTools = $savedProjectTools
    $Script:OperationContext = $savedOperationContext
    Remove-Item -LiteralPath $trackedPlanRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not (Test-GameWipQualityPolicyChange -Files @('.clang-format')) -or
    -not (Test-GameWipQualityPolicyChange -Files @('config/quality/ruff.toml')) -or
    (Test-GameWipQualityPolicyChange -Files @('game/main.cpp')))
{
    throw 'Changed-quality policy invalidation does not distinguish policy changes from ordinary source changes.'
}
$untrackedRelative = 'scripts/gamewip_quality_untracked_' + [guid]::NewGuid().ToString('N') + '.py'
$untrackedPath = Join-Path $repositoryRoot $untrackedRelative
try
{
    [IO.File]::WriteAllText($untrackedPath, 'print("quality scope")', [Text.UTF8Encoding]::new($false))
    if (@(Get-GameWipMaintainedWorktreeFile -Extensions @('.py')) -notcontains $untrackedRelative.Replace('\', '/'))
    {
        throw 'Full quality discovery skipped a first-party untracked Python file.'
    }
}
finally
{
    Remove-Item -LiteralPath $untrackedPath -Force -ErrorAction SilentlyContinue
}

$informational = Get-GameWipProjectTool -Id unicode
if ([bool]$informational.capabilities.update)
{
    throw 'Informational Unicode resource unexpectedly became installable.'
}

$standardizationScript = Join-Path $repositoryRoot '.github\scripts\check_helper_standardization.py'
$qualityOwnershipScript = Join-Path $repositoryRoot '.github\scripts\check_quality_ownership.py'
if (-not (Test-Path -LiteralPath $standardizationScript) -or -not (Test-Path -LiteralPath $qualityOwnershipScript))
{
    throw 'Repository standardization self-checkers are missing.'
}

Write-Host 'Project helper regression tests passed.'
