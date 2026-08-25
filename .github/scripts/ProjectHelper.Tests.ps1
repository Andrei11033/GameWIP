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
foreach ($requiredHelpText in @('gamewip.bat <action> [command] [target]', 'Available commands', 'quality', 'tools', 'runs', '-Preview', '-NonInteractive', '-Yes', '-NoBuild', '-Json'))
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

# Atomic persistence can replace an existing file without failing while
# PowerShell resolves the typed fallback catches.
$atomicRoot = Join-Path $repositoryRoot ('build\gamewip\temp\atomic-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $atomicRoot | Out-Null
try
{
    $atomicPath = Join-Path $atomicRoot 'state.txt'
    Write-GameWipTextAtomic -Path $atomicPath -Content 'first'
    Write-GameWipTextAtomic -Path $atomicPath -Content 'second'
    if ((Get-Content -Raw -LiteralPath $atomicPath) -ne 'second')
    {
        throw 'Atomic persistence did not replace an existing file.'
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
