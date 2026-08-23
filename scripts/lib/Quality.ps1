# GameWIP Quality helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Get-GameWipQualityTool
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $toolInfo = @($ProjectTools.tools | Where-Object { $_.id -eq $Id } | Select-Object -First 1)
    if ($toolInfo.Count -eq 0)
    {
        throw "Quality tool '$Id' is not registered."
    }
    $detected = Get-GameWipDetectedTool -Tool $toolInfo[0]
    if ((Get-GameWipToolCompatibility -Tool $toolInfo[0] -Detected $detected) -ne 'compatible')
    {
        throw "Quality tool '$Id' is missing or does not satisfy its declared version. Run setup.bat repair."
    }
    return $detected.Location
}

function Invoke-GameWipQualityNative
{
    param([string]$Name, [string]$FilePath, [string[]]$Arguments = @(), [hashtable]$Environment = @{})
    $previousValues = @{}
    foreach ($entry in $Environment.GetEnumerator())
    {
        $previousValues[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, [string]$entry.Value, 'Process')
    }
    try
    {
        Invoke-GameWipNative -Name $Name -FilePath $FilePath -Arguments $Arguments
    }
    finally
    {
        foreach ($entry in $previousValues.GetEnumerator())
        {
            [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
        }
    }
}

function Get-GameWipPowerShellFile
{
    return @(Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File -Filter '*.ps1' | Where-Object { $_.FullName -notmatch '[\\/](?:build|external)[\\/]' })
}

function Get-GameWipCMakeFile
{
    return @(
        Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File |
            Where-Object {
                $_.FullName -notmatch '[\\/](?:build|external)[\\/]' -and
                ($_.Name -eq 'CMakeLists.txt' -or $_.Name -match '\.cmake(?:\.in)?$')
            } |
            ForEach-Object { $_.FullName }
    )
}

function Invoke-GameWipPowerShellQuality
{
    param([switch]$Fix)
    $moduleRoot = Get-GameWipQualityTool -Id 'psscriptanalyzer'
    Import-Module (Join-Path $moduleRoot 'PSScriptAnalyzer.psd1') -Force
    $settings = Join-Path $RepositoryRoot 'config\quality\psscriptanalyzer.psd1'
    $failures = [System.Collections.Generic.List[object]]::new()
    foreach ($file in Get-GameWipPowerShellFile)
    {
        $current = Get-Content -Raw -LiteralPath $file.FullName
        $formatted = Invoke-Formatter -ScriptDefinition $current -Settings $settings
        $formatted = [regex]::Replace($formatted, '(?m)[ \t]+$', '')
        $expected = $formatted.TrimEnd() + "`n"
        if ($Fix)
        {
            [IO.File]::WriteAllText($file.FullName, $expected, [Text.UTF8Encoding]::new($false))
        }
        elseif ($current.Replace("`r`n", "`n") -cne $expected)
        {
            throw "PowerShell formatting differs for '$($file.FullName)'. Run .\gamewip.bat quality -QualityAction fix."
        }
        foreach ($finding in @(Invoke-ScriptAnalyzer -Path $file.FullName -Settings $settings -Severity Warning, Error))
        {
            $failures.Add($finding)
        }
    }
    if ($failures.Count -ne 0)
    {
        $failures | Format-Table -AutoSize | Out-Host
        throw "PSScriptAnalyzer reported $($failures.Count) warning/error finding(s)."
    }
}

function Invoke-GameWipQualityCheck
{
    $ruff = Get-GameWipQualityTool -Id 'ruff'
    $eslint = Get-GameWipQualityTool -Id 'eslint'
    $prettier = Get-GameWipQualityTool -Id 'prettier'
    $gersemi = Get-GameWipQualityTool -Id 'gersemi'
    $yamllint = Get-GameWipQualityTool -Id 'yamllint'
    $markdownlint = Get-GameWipQualityTool -Id 'markdownlint-cli2'
    $actionlint = Get-GameWipQualityTool -Id 'actionlint'
    $python = (Resolve-GameWipPython).Path

    $qualityConfig = Join-Path $RepositoryRoot 'config\quality'
    $ruffConfig = Join-Path $qualityConfig 'ruff.toml'
    $eslintConfig = Join-Path $qualityConfig 'eslint.config.js'
    $prettierConfig = Join-Path $qualityConfig 'prettier.json'
    $prettierIgnore = Join-Path $qualityConfig 'prettier.ignore'
    $gersemiConfig = Join-Path $qualityConfig 'gersemi.yml'
    $yamllintConfig = Join-Path $qualityConfig 'yamllint.yml'
    $markdownlintConfig = Join-Path $qualityConfig 'markdownlint-cli2.jsonc'

    if (Test-GameWipWindowsHost)
    {
        $nodePath = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) 'npm\lib\node_modules'
    }
    else
    {
        $nodePath = (& npm root --global).Trim()
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($nodePath))
        {
            throw 'Unable to resolve the global npm module root for ESLint.'
        }
    }

    Invoke-GameWipFormat -Mode check
    Invoke-GameWipQualityNative -Name 'ruff-check' -FilePath $ruff -Arguments @('check', '--config', $ruffConfig, '.github/scripts', 'foundation/unicode/tools')
    Invoke-GameWipQualityNative -Name 'ruff-format-check' -FilePath $ruff -Arguments @('format', '--check', '--config', $ruffConfig, '.github/scripts', 'foundation/unicode/tools')
    Invoke-GameWipPowerShellQuality
    Invoke-GameWipQualityNative -Name 'eslint' -FilePath $eslint -Arguments @('--config', $eslintConfig, '.github/scripts') -Environment @{ NODE_PATH = $nodePath }
    Invoke-GameWipQualityNative -Name 'prettier-check' -FilePath $prettier -Arguments @('--config', $prettierConfig, '--ignore-path', $prettierIgnore, '--check', '**/*.{js,json,jsonc,yml,yaml,css}')
    Invoke-GameWipQualityNative -Name 'gersemi-check' -FilePath $gersemi -Arguments (@('--config', $gersemiConfig, '--check') + @(Get-GameWipCMakeFile))
    Invoke-GameWipQualityNative -Name 'yamllint' -FilePath $yamllint -Arguments @('-c', $yamllintConfig, '.')
    Invoke-GameWipQualityNative -Name 'actionlint' -FilePath $actionlint -Arguments @('-color')
    Invoke-GameWipQualityNative -Name 'markdownlint' -FilePath $markdownlint -Arguments @('--config', $markdownlintConfig)
    Invoke-GameWipQualityNative -Name 'schema-validation' -FilePath $python -Arguments @('.github/scripts/validate_config_schemas.py')
    Invoke-GameWipQualityNative -Name 'repository-standards' -FilePath $python -Arguments @('.github/scripts/check_repository_standards.py')
    Invoke-GameWipQualityNative -Name 'documentation-standards' -FilePath $python -Arguments @('.github/scripts/check_documentation_standards.py')
    Invoke-GameWipMarkdownLink
}

function Invoke-GameWipQualityFix
{
    $qualityConfig = Join-Path $RepositoryRoot 'config\quality'
    $ruffConfig = Join-Path $qualityConfig 'ruff.toml'
    $prettierConfig = Join-Path $qualityConfig 'prettier.json'
    $prettierIgnore = Join-Path $qualityConfig 'prettier.ignore'
    $gersemiConfig = Join-Path $qualityConfig 'gersemi.yml'

    Invoke-GameWipFormat -Mode apply
    Invoke-GameWipQualityNative -Name 'ruff-format' -FilePath (Get-GameWipQualityTool -Id 'ruff') -Arguments @('format', '--config', $ruffConfig, '.github/scripts', 'foundation/unicode/tools')
    Invoke-GameWipPowerShellQuality -Fix
    Invoke-GameWipQualityNative `
        -Name 'prettier-write' `
        -FilePath (Get-GameWipQualityTool -Id 'prettier') `
        -Arguments @('--config', $prettierConfig, '--ignore-path', $prettierIgnore, '--write', '**/*.{js,json,jsonc,yml,yaml,css}')
    Invoke-GameWipQualityNative `
        -Name 'gersemi-format' `
        -FilePath (Get-GameWipQualityTool -Id 'gersemi') `
        -Arguments (@('--config', $gersemiConfig, '--in-place') + @(Get-GameWipCMakeFile))
    Invoke-GameWipQualityCheck
}

function Invoke-GameWipQuality
{
    param([ValidateSet('check', 'fix')][string]$Mode = 'check')
    Initialize-GameWipStorage
    if ($Mode -eq 'fix')
    {
        Invoke-GameWipQualityFix
    }
    else
    {
        Invoke-GameWipQualityCheck
    }
}
