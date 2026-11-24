# Quality scope and maintained-file selection.

# ------------------------------------------------------------
# Quality scope and policy selection
# ------------------------------------------------------------

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Quality scope discovery
# ------------------------------------------------------------

function Get-GameWipQualityTool
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $toolInfo = Get-GameWipProjectTool -Id $Id
    $status = Get-GameWipToolStatus -Tool $toolInfo

    if (-not $status.Ready)
    {
        $details = @("Tool state: $($status.State)") + @($status.DependencyFailures | ForEach-Object { "$($_.Package): $($_.State)" })
        $helperCommand = Get-GameWipProjectHelperCommand
        throw (New-GameWipDiagnosticException `
                -Code 'quality-tool-unavailable' `
                -Summary "Quality tool '$Id' is missing or incompatible." `
                -Details ($details -join '; ') `
                -SuggestedActions @("$helperCommand tool ensure quality -Yes", "$helperCommand tool status"))
    }

    return [string]$status.Detected.Location
}

function Select-GameWipQualityFile
{
    param([string[]]$Files, [string[]]$Extensions = @(), [string[]]$ExactNames = @())
    return @($Files | Where-Object {
            $leaf = [IO.Path]::GetFileName($_)
            $extension = [IO.Path]::GetExtension($_).ToLowerInvariant()
            $ExactNames -contains $leaf -or $Extensions -contains $extension
        })
}

function Get-GameWipMaintainedWorktreeFile
{
    param([string[]]$Extensions = @(), [string[]]$ExactNames = @())
    $result = Invoke-GameWipProcess -FilePath git -Arguments @('-C', $RepositoryRoot, 'ls-files', '--cached', '--others', '--exclude-standard') -OutputMode LogOnly -TimeoutSeconds 30

    if ($result.ExitCode -ne 0)
    {
        throw 'Could not enumerate maintained worktree files for repository quality.'
    }

    $files = @($result.Stdout |
            ForEach-Object { ([string]$_).Replace('\', '/') } |
            Where-Object { $_ -and $_ -notmatch '^(?:external|build|install|docs/releases)/' } |
            Where-Object { Test-Path -LiteralPath (Join-Path $RepositoryRoot $_) -PathType Leaf } |
            Sort-Object -Unique)

    if (@($ExactNames).Count -eq 0 -and @($Extensions).Count -eq 0)
    {
        return $files
    }

    return Select-GameWipQualityFile -Files $files -Extensions $Extensions -ExactNames $ExactNames
}

function Get-GameWipQualityFileSelections
{
    param([Parameter(Mandatory = $true)]$ScopeInfo)

    $useChangedScope = [bool]$ScopeInfo.UseChanged

    # Derive every tool scope from one maintained-file snapshot.
    $files = if ($useChangedScope)
    {
        @($ScopeInfo.Files)
    }
    else
    {
        @(Get-GameWipMaintainedWorktreeFile)
    }

    $cppFiles = if ($useChangedScope)
    {
        Select-GameWipQualityFile -Files $files -Extensions @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl')
    }
    else
    {
        @()
    }
    $pythonFiles = Select-GameWipQualityFile -Files $files -Extensions @('.py')
    $powerShellFiles = if ($useChangedScope)
    {
        Select-GameWipQualityFile -Files $files -Extensions @('.ps1', '.psd1', '.psm1')
    }
    else
    {
        @()
    }
    $javaScriptFiles = Select-GameWipQualityFile -Files $files -Extensions @('.js', '.mjs', '.cjs')
    $prettierFiles = Select-GameWipQualityFile -Files $files -Extensions @('.js', '.mjs', '.cjs', '.json', '.jsonc', '.yml', '.yaml', '.css')
    $specialJsonFiles = Select-GameWipQualityFile -Files $files -ExactNames @('.vsconfig', 'GameWIP.code-workspace')
    $cmakeFiles = if ($files.Count -eq 0)
    {
        @()
    }
    else
    {
        Get-GameWipCMakeFile -Files $files
    }
    $yamlFiles = Select-GameWipQualityFile -Files $files -Extensions @('.yml', '.yaml')
    $markdownFiles = Select-GameWipQualityFile -Files $files -Extensions @('.md')

    return [pscustomobject]@{
        UseChangedScope = $useChangedScope

        # Source-language formatters
        Cpp = @($cppFiles)
        Python = @($pythonFiles)
        PowerShell = @($powerShellFiles)
        JavaScript = @($javaScriptFiles)

        # Structured and documentation formats
        Prettier = @($prettierFiles)
        SpecialJson = @($specialJsonFiles)
        CMake = @($cmakeFiles)
        Yaml = @($yamlFiles)
        Markdown = @($markdownFiles)
    }
}

function Test-GameWipQualityPolicyChange
{
    param([string[]]$Files)

    foreach ($file in @($Files))
    {
        $path = ([string]$file).Replace('\', '/')

        if ($path -in @('.clang-format', '.clang-tidy', '.editorconfig', 'scripts/config/commands.json', 'scripts/config/project-tools.json') -or
            $path.StartsWith('config/quality/', (Get-GameWipPathComparison)))
        {
            return $true
        }
    }

    return $false
}

function Get-GameWipQualityScope
{
    param([switch]$Changed)

    if (-not $Changed)
    {
        return [pscustomobject]@{
            Requested = $false
            UseChanged = $false
            Expanded = $false
            Files = @()
        }
    }

    $files = @(Get-GameWipChangedRepositoryFile)
    $expanded = Test-GameWipQualityPolicyChange -Files $files
    return [pscustomobject]@{
        Requested = $true
        UseChanged = -not $expanded
        Expanded = $expanded
        Files = @($files)
    }
}

function Get-GameWipPowerShellFile
{
    param([string[]]$Files)
    $extensions = @('.ps1', '.psd1', '.psm1')
    $selected = if ($null -ne $Files -and $Files.Count -ne 0)
    {
        @($Files)
    }
    else
    {
        @(Get-GameWipMaintainedWorktreeFile -Extensions $extensions)
    }

    return @($selected |
            ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
            Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and $extensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() } |
            ForEach-Object { Get-Item -LiteralPath $_ })
}

function Get-GameWipCMakeFile
{
    param([string[]]$Files)
    $selected = if ($null -ne $Files -and $Files.Count -ne 0)
    {
        @($Files)
    }
    else
    {
        @(Get-GameWipMaintainedWorktreeFile -ExactNames @('CMakeLists.txt') -Extensions @('.cmake', '.in'))
    }

    return @($selected |
            ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
            Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and ((Split-Path -Leaf $_) -eq 'CMakeLists.txt' -or $_ -match '\.cmake(?:\.in)?$') } |
            Sort-Object -Unique)
}

function Invoke-GameWipLineEndingNormalization
{
    # Formatters may choose their own platform newline or preserve trailing
    # whitespace. Apply the repository's text policy after every formatter has finished.
    param([string[]]$Files = @())

    $candidates = @(
        if ($null -ne $Files -and @($Files).Count -ne 0)
        {
            $Files |
                ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Sort-Object -Unique
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile |
                ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Sort-Object -Unique
        }
    )

    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $normalizedCount = 0
    $skippedCount = 0

    foreach ($file in $candidates)
    {
        $bytes = [IO.File]::ReadAllBytes($file)

        if ($bytes -contains [byte]0)
        {
            $skippedCount++
            continue
        }

        try
        {
            $text = $utf8.GetString($bytes)
        }

        catch [Text.DecoderFallbackException]
        {
            $skippedCount++
            continue
        }

        $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
        $normalized = [regex]::Replace($normalized, '(?m)[ \t]+$', '')

        if ($normalized -cne $text)
        {
            Write-GameWipTextAtomic -Path $file -Content $normalized
            Add-GameWipOperationChange -Message "Normalized text whitespace in $(Get-GameWipRepositoryRelativePath -Path $file)"
            $normalizedCount++
        }
    }

    Write-GameWipStatusLine `
        -Status pass `
        -Text "Text normalization: checked $($candidates.Count) maintained file(s), normalized LF/trailing whitespace in $normalizedCount, skipped $skippedCount non-text/binary file(s)." `
        -Semantic Success `
        -Indent 2
}
