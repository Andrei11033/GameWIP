# Optional repository-hygiene audits. Findings are evidence, not automatic edits.

# ------------------------------------------------------------
# Hygiene configuration and audit reporting
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Assert-GameWipHygieneConfig
{
    $checks = @($HygieneConfig.Checks)
    $profiles = @($HygieneConfig.Profiles)
    $explanations = @($HygieneConfig.Explanations)
    Assert-GameWipUniqueId -Label 'hygiene check' -Items $checks
    Assert-GameWipUniqueId -Label 'hygiene profile' -Items $profiles
    Assert-GameWipUniqueId -Label 'hygiene explanation' -Items $explanations

    $checkIds = @($checks | ForEach-Object { [string]$_.Id })
    if (@($profiles | Where-Object Id -eq $HygieneConfig.DefaultProfile).Count -ne 1)
    {
        throw "Unknown default hygiene profile '$($HygieneConfig.DefaultProfile)'."
    }
    foreach ($check in $checks)
    {
        if ($check.Availability -eq 'available' -and $check.Provider -eq 'clang-tidy' -and -not $check.ContainsKey('Rules'))
        {
            throw "Available hygiene check '$($check.Id)' must declare clang-tidy rules."
        }
        if ($check.Availability -eq 'available' -and $check.Provider -eq 'compiler-warning' -and
            (-not $check.ContainsKey('Rules') -or -not $check.ContainsKey('Flags')))
        {
            throw "Available compiler-warning check '$($check.Id)' must declare compiler flags."
        }
        if ($check.Availability -eq 'planned' -and ($check.Provider -ne 'future' -or $check.ContainsKey('Rules')))
        {
            throw "Planned hygiene check '$($check.Id)' must not declare executable rules."
        }
    }
    foreach ($profileInfo in $profiles)
    {
        foreach ($checkId in @($profileInfo.Checks))
        {
            if ($checkIds -notcontains [string]$checkId)
            {
                throw "Hygiene profile '$($profileInfo.Id)' references unknown check '$checkId'."
            }
        }
    }
    foreach ($explanation in $explanations)
    {
        try
        {
            [regex]::new([string]$explanation.PathPattern) | Out-Null
        }
        catch
        {
            throw "Hygiene explanation '$($explanation.Id)' has an invalid path pattern."
        }
        foreach ($checkId in @($explanation.Checks))
        {
            if ($checkIds -notcontains [string]$checkId)
            {
                throw "Hygiene explanation '$($explanation.Id)' references unknown check '$checkId'."
            }
        }
    }
}

function Get-GameWipHygieneCheck
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $checkMatches = @($HygieneConfig.Checks | Where-Object Id -eq $Id)
    if ($checkMatches.Count -ne 1)
    {
        throw "Unknown hygiene check '$Id'. Run '.\gamewip.bat quality hygiene list'."
    }
    return $checkMatches[0]
}

function Get-GameWipHygieneSelection
{
    param([string]$Selector)
    $effectiveSelector = $Selector
    if ([string]::IsNullOrWhiteSpace($effectiveSelector))
    {
        $effectiveSelector = [string]$HygieneConfig.DefaultProfile
    }
    $profileMatches = @($HygieneConfig.Profiles | Where-Object Id -eq $effectiveSelector)
    if ($profileMatches.Count -eq 1)
    {
        return [pscustomobject]@{
            Id = [string]$profileMatches[0].Id
            Title = [string]$profileMatches[0].Title
            Checks = @($profileMatches[0].Checks | ForEach-Object { Get-GameWipHygieneCheck -Id ([string]$_) })
        }
    }
    $check = Get-GameWipHygieneCheck -Id $effectiveSelector
    return [pscustomobject]@{ Id = [string]$check.Id; Title = [string]$check.Title; Checks = @($check) }
}

function Show-GameWipHygieneList
{
    Write-GameWipSection 'Hygiene profiles'
    foreach ($profileInfo in @($HygieneConfig.Profiles))
    {
        Write-Host ('  {0,-12} {1} ({2})' -f $profileInfo.Id, $profileInfo.Title, (@($profileInfo.Checks) -join ', '))
    }
    Write-Host ''
    Write-GameWipSection 'Hygiene checks'
    foreach ($check in @($HygieneConfig.Checks))
    {
        Write-Host ('  {0,-24} {1,-9} {2,-11} {3}' -f $check.Id, $check.Availability, $check.Confidence, $check.Title)
    }
}

function Show-GameWipHygieneStatus
{
    $tidy = Get-GameWipDetectedTool -Tool (Get-GameWipProjectTool -Id clang-tidy)
    $runner = Resolve-GameWipToolCommand -Command run-clang-tidy
    $python = Resolve-GameWipToolCommand -Command python
    $database = Join-Path $RepositoryRoot "build\$($HygieneConfig.AnalyzePreset)\compile_commands.json"
    Write-GameWipSection 'Hygiene configuration status'
    Write-Host "  Registry:             config/quality/hygiene.json"
    Write-Host "  Default profile:      $($HygieneConfig.DefaultProfile)"
    Write-Host "  Available checks:     $(@($HygieneConfig.Checks | Where-Object Availability -eq available).Count)"
    Write-Host "  Planned checks:       $(@($HygieneConfig.Checks | Where-Object Availability -eq planned).Count)"
    Write-Host "  clang-tidy:           $(if ($tidy.Installed) { $tidy.Location } else { 'unavailable' })"
    Write-Host "  run-clang-tidy:       $(if ($runner) { $runner } else { 'unavailable' })"
    Write-Host "  Python:               $(if ($python) { $python } else { 'unavailable' })"
    Write-Host '  compiler warnings:    supported through the analyze compilation database'
    Write-Host "  Compilation database: $(if (Test-Path -LiteralPath $database) { $database } else { 'not configured (created on first audit)' })"
}

function Get-GameWipHygieneExplanation
{
    param([string]$CheckId, [string]$Path)
    $normalized = $Path.Replace('\', '/')
    foreach ($explanation in @($HygieneConfig.Explanations))
    {
        if (@($explanation.Checks) -contains $CheckId -and $normalized -match [string]$explanation.PathPattern)
        {
            return $explanation
        }
    }
    return $null
}

function ConvertFrom-GameWipClangTidyFinding
{
    param([string[]]$Lines, [hashtable]$RuleLookup)
    $findings = [System.Collections.Generic.List[object]]::new()
    $pattern = '^(?<path>.*):(?<line>[0-9]+):(?<column>[0-9]+):\s+(?<severity>warning|error|note):\s+(?<message>.*?)\s+\[(?<rule>[^]]+)\]\s*$'
    foreach ($line in @($Lines))
    {
        $match = [regex]::Match([string]$line, $pattern)
        if (-not $match.Success -or $match.Groups['severity'].Value -eq 'note')
        {
            continue
        }
        $rule = $match.Groups['rule'].Value
        if (-not $RuleLookup.ContainsKey($rule))
        {
            continue
        }
        $check = $RuleLookup[$rule]
        $path = $match.Groups['path'].Value
        if ([IO.Path]::IsPathRooted($path))
        {
            $path = Get-GameWipRepositoryRelativePath -Path $path
        }
        else
        {
            $path = $path.Replace('\', '/')
        }
        $explanation = Get-GameWipHygieneExplanation -CheckId ([string]$check.Id) -Path $path
        $confidence = [string]$check.Confidence
        $suggestedAction = 'Review the diagnostic in context; do not remove code automatically.'
        $explanationId = $null
        if ($null -ne $explanation)
        {
            $confidence = 'EXPLAINED'
            $suggestedAction = [string]$explanation.Reason
            $explanationId = [string]$explanation.Id
        }
        $findings.Add([ordered]@{
                check = [string]$check.Id
                rule = $rule
                confidence = $confidence
                path = $path
                line = [int]$match.Groups['line'].Value
                column = [int]$match.Groups['column'].Value
                message = $match.Groups['message'].Value
                evidence = "$(if ($rule.StartsWith('-W')) { 'compiler warning' } else { 'clang-tidy diagnostic' }) '$rule'"
                suggestedAction = $suggestedAction
                explanation = $explanationId
            }) | Out-Null
    }
    return @($findings | Sort-Object { $_.path }, { $_.line }, { $_.column }, { $_.rule })
}

function Invoke-GameWipHygieneClangTidy
{
    param([object[]]$Checks)
    $tidy = Get-GameWipQualityTool -Id clang-tidy
    $runner = Resolve-GameWipToolCommand -Command run-clang-tidy
    $python = Resolve-GameWipToolCommand -Command python
    if (-not $runner -or -not $python)
    {
        throw (New-GameWipDiagnosticException -Code 'hygiene-tool-unavailable' -Summary 'The hygiene audit requires Python and run-clang-tidy.' -Details 'The configured MSYS2 UCRT64 analysis toolchain is incomplete.' -SuggestedActions @('.\gamewip.bat tools status', '.\setup.bat repair'))
    }

    $preset = [string]$HygieneConfig.AnalyzePreset
    Invoke-GameWipConfigurePreset -Name $preset | Out-Null
    $databaseRoot = Join-Path $RepositoryRoot "build\$preset"
    $database = Join-Path $databaseRoot 'compile_commands.json'
    if (-not (Test-Path -LiteralPath $database -PathType Leaf))
    {
        throw "Analyze preset '$preset' did not produce compile_commands.json."
    }

    $ruleLookup = @{}
    $options = [ordered]@{}
    foreach ($check in @($Checks))
    {
        foreach ($rule in @($check.Rules))
        {
            $ruleLookup[[string]$rule] = $check
        }
        if ($check.ContainsKey('Options'))
        {
            foreach ($entry in $check.Options.GetEnumerator())
            {
                $options[[string]$entry.Key] = [string]$entry.Value
            }
        }
    }
    $checkOptions = @($options.GetEnumerator() | ForEach-Object { [ordered]@{ key = $_.Key; value = $_.Value } })
    $inlineConfig = [ordered]@{ Checks = '-*,' + (($ruleLookup.Keys | Sort-Object) -join ','); WarningsAsErrors = ''; CheckOptions = $checkOptions } | ConvertTo-Json -Compress -Depth 5
    $arguments = @(
        '-u', $runner,
        '-p', $databaseRoot,
        '-clang-tidy-binary', $tidy,
        '-config', $inlineConfig,
        '-source-filter', [string]$HygieneConfig.SourceFilter,
        '-header-filter', [string]$HygieneConfig.HeaderFilter,
        '-exclude-header-filter', [string]$HygieneConfig.ExcludedHeaderFilter,
        '-j', '4',
        '-quiet'
    )
    $path = @((Get-GameWipToolchainPathPrefix $preset), $env:PATH) -join [IO.Path]::PathSeparator
    $result = Invoke-GameWipProcess -FilePath $python -Arguments $arguments -OutputMode $Script:OperationContext.OutputMode -TimeoutSeconds 7200 -Environment @{ PATH = $path }
    $lines = @($result.Stdout) + @($result.Stderr)
    $findings = @(ConvertFrom-GameWipClangTidyFinding -Lines $lines -RuleLookup $ruleLookup)
    $providerFailures = @($lines | Where-Object { $_ -match ':[0-9]+:[0-9]+:\s+error:' -or $_ -match '^(?:Error while processing|Error:)' })
    if ($providerFailures.Count -ne 0)
    {
        throw "run-clang-tidy reported $($providerFailures.Count) compiler or provider error(s). Inspect the retained process log."
    }
    if ($result.ExitCode -ne 0 -and $findings.Count -eq 0)
    {
        throw "run-clang-tidy failed with exit code $($result.ExitCode) without producing a recognized hygiene finding."
    }
    return $findings
}

function Invoke-GameWipHygieneCompilerWarnings
{
    param([object[]]$Checks)
    $python = Resolve-GameWipToolCommand -Command python
    if (-not $python)
    {
        throw (New-GameWipDiagnosticException `
                -Code 'hygiene-tool-unavailable' `
                -Summary 'The compiler-warning hygiene audit requires Python.' `
                -Details 'The configured Python interpreter could not be resolved.' `
                -SuggestedActions @('.\gamewip.bat tools status', '.\setup.bat repair'))
    }

    $preset = [string]$HygieneConfig.AnalyzePreset
    $databaseRoot = Join-Path $RepositoryRoot "build\$preset"
    $database = Join-Path $databaseRoot 'compile_commands.json'
    $scriptPath = Join-Path $ScriptsRoot 'lib\hygiene_compiler_warnings.py'
    $ruleLookup = @{}
    $flags = [System.Collections.Generic.List[string]]::new()
    foreach ($check in @($Checks))
    {
        foreach ($rule in @($check.Rules))
        {
            $ruleLookup[[string]$rule] = $check
        }
        foreach ($flag in @($check.Flags))
        {
            $flags.Add([string]$flag)
        }
    }
    $arguments = @('-u', $scriptPath, '--database', $database, '--source-filter', [string]$HygieneConfig.SourceFilter)
    foreach ($flag in $flags)
    {
        $arguments += "--flag=$flag"
    }
    $path = @((Get-GameWipToolchainPathPrefix $preset), $env:PATH) -join [IO.Path]::PathSeparator
    $result = Invoke-GameWipProcess -FilePath $python -Arguments $arguments -OutputMode $Script:OperationContext.OutputMode -TimeoutSeconds 7200 -Environment @{ PATH = $path }
    $lines = @($result.Stdout) + @($result.Stderr)
    $findings = @(ConvertFrom-GameWipClangTidyFinding -Lines $lines -RuleLookup $ruleLookup)
    $providerFailures = @($lines | Where-Object { $_ -match 'compiler-warning provider failed' -or $_ -match ':[0-9]+:[0-9]+:\s+error:' })
    if ($providerFailures.Count -ne 0)
    {
        throw "Compiler-warning provider reported $($providerFailures.Count) failure(s). Inspect the retained process log."
    }
    if ($result.ExitCode -ne 0)
    {
        throw "Compiler-warning provider failed with exit code $($result.ExitCode). Inspect the retained process log."
    }
    return $findings
}

function Write-GameWipHygieneReport
{
    param([Parameter(Mandatory = $true)]$Report)
    $path = Join-Path $Script:OperationContext.Run.Artifacts 'hygiene-report.json'
    Write-GameWipTextAtomic -Path $path -Content (($Report | ConvertTo-Json -Depth 8) + "`n")
    Write-GameWipSection 'Hygiene audit'
    Write-Host "  Selection: $($Report.selection)"
    Write-Host "  Findings:  $(@($Report.findings).Count)"
    Write-Host "  Planned:   $(@($Report.planned).Count)"
    Write-Host "  Report:    $path"
    $confidenceCounts = @($Report.findings | Group-Object { $_.confidence } | Sort-Object Name)
    if ($confidenceCounts.Count -ne 0)
    {
        Write-Host ('  Confidence: {0}' -f (($confidenceCounts | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join ', '))
    }
    $displayLimit = 25
    foreach ($finding in @($Report.findings | Select-Object -First $displayLimit))
    {
        Write-Host ('  [{0}] {1}:{2} {3}: {4}' -f $finding.confidence, $finding.path, $finding.line, $finding.rule, $finding.message)
    }
    if (@($Report.findings).Count -gt $displayLimit)
    {
        Write-Host "  ... $(@($Report.findings).Count - $displayLimit) additional finding(s) are retained in the JSON report."
    }
    foreach ($planned in @($Report.planned))
    {
        Write-Host "  [INFORMATION] $($planned.id): planned provider; no result claimed"
    }
}

function Invoke-GameWipHygieneAudit
{
    param([string]$Selector, [switch]$Enforce)
    $selection = Get-GameWipHygieneSelection -Selector $Selector
    $available = @($selection.Checks | Where-Object Availability -eq available)
    $planned = @($selection.Checks | Where-Object Availability -eq planned | ForEach-Object { [ordered]@{ id = $_.Id; title = $_.Title; status = 'planned' } })
    $findings = @()
    if ($available.Count -ne 0)
    {
        $clangTidyChecks = @($available | Where-Object Provider -eq 'clang-tidy')
        $compilerWarningChecks = @($available | Where-Object Provider -eq 'compiler-warning')
        if ($clangTidyChecks.Count -ne 0)
        {
            $findings += @(Invoke-GameWipHygieneClangTidy -Checks $clangTidyChecks)
        }
        if ($compilerWarningChecks.Count -ne 0)
        {
            $findings += @(Invoke-GameWipHygieneCompilerWarnings -Checks $compilerWarningChecks)
        }
    }
    $report = [ordered]@{
        schemaVersion = 1
        generatedAt = (Get-Date).ToUniversalTime().ToString('o')
        selection = $selection.Id
        enforcement = [bool]$Enforce
        checks = @($selection.Checks | ForEach-Object { [string]$_.Id })
        findings = $findings
        planned = $planned
    }
    Write-GameWipHygieneReport -Report $report
    if ($Enforce)
    {
        $proven = @($findings | Where-Object confidence -eq PROVEN)
        if ($proven.Count -ne 0)
        {
            throw (New-GameWipDiagnosticException -Code 'hygiene-proven-findings' -Summary "$($proven.Count) proven hygiene finding(s) require review." -Details 'The complete finding report is retained in the operation artifacts.' -SuggestedActions @('Review each PROVEN finding.', 'Fix or centrally explain only after confirming the evidence.'))
        }
    }
}
