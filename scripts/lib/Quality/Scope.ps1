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
    $markdownFiles = Select-GameWipQualityFile -Files $files -Extensions @('.md', '.markdown', '.dox')

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

function Get-GameWipQualityCheckPlan
{
    param([Parameter(Mandatory = $true)]$ScopeInfo)

    $allChecks = @(
        'clang-format', 'ruff lint', 'ruff format', 'PowerShell', 'ESLint', 'Prettier',
        'Prettier special JSON', 'Gersemi', 'yamllint', 'actionlint', 'markdownlint',
        'JSON Schema', 'quality ownership', 'helper standardization', 'repository standards',
        'repository standards tests', 'documentation standards', 'Markdown links'
    )
    $allTools = @(
        'python', 'clang-format', 'ruff', 'psscriptanalyzer', 'eslint', 'prettier', 'gersemi',
        'yamllint', 'markdownlint-cli2', 'actionlint', 'jsonschema'
    )

    if (-not $ScopeInfo.UseChanged)
    {
        return [pscustomobject]@{
            Checks = $allChecks
            Tools = $allTools
        }
    }

    $files = @($ScopeInfo.Files | ForEach-Object { ([string]$_).Replace('\', '/') })
    $selection = Get-GameWipQualityFileSelections -ScopeInfo $ScopeInfo
    $checks = [System.Collections.Generic.List[string]]::new()
    $tools = [System.Collections.Generic.List[string]]::new()

    # Select language and format checks from changed file types.
    if ($selection.Cpp.Count -ne 0)
    {
        $checks.Add('clang-format') | Out-Null
        $tools.Add('clang-format') | Out-Null
    }
    if ($selection.Python.Count -ne 0)
    {
        $checks.Add('ruff lint') | Out-Null
        $checks.Add('ruff format') | Out-Null
        $tools.Add('ruff') | Out-Null
    }
    if ($selection.PowerShell.Count -ne 0)
    {
        $checks.Add('PowerShell') | Out-Null
        $tools.Add('psscriptanalyzer') | Out-Null
    }
    if ($selection.JavaScript.Count -ne 0)
    {
        $checks.Add('ESLint') | Out-Null
        $tools.Add('eslint') | Out-Null
    }
    if ($selection.Prettier.Count -ne 0)
    {
        $checks.Add('Prettier') | Out-Null
        $tools.Add('prettier') | Out-Null
    }
    if ($selection.SpecialJson.Count -ne 0)
    {
        $checks.Add('Prettier special JSON') | Out-Null
        $tools.Add('prettier') | Out-Null
    }
    if ($selection.CMake.Count -ne 0)
    {
        $checks.Add('Gersemi') | Out-Null
        $tools.Add('gersemi') | Out-Null
    }
    if ($selection.Yaml.Count -ne 0)
    {
        $checks.Add('yamllint') | Out-Null
        $tools.Add('yamllint') | Out-Null
    }
    if ($selection.Markdown.Count -ne 0)
    {
        $checks.Add('markdownlint') | Out-Null
        $tools.Add('markdownlint-cli2') | Out-Null
        $checks.Add('documentation standards') | Out-Null
        $checks.Add('Markdown links') | Out-Null
    }

    # Repository-wide contracts run only when their owned files change.
    if (@($files | Where-Object { [IO.Path]::GetExtension($_).ToLowerInvariant() -in @('.json', '.jsonc') }).Count -ne 0)
    {
        $checks.Add('JSON Schema') | Out-Null
        $tools.Add('jsonschema') | Out-Null
    }
    if (@($files | Where-Object { $_ -like '.github/workflows/*.yml' -or $_ -like '.github/workflows/*.yaml' }).Count -ne 0)
    {
        $checks.Add('actionlint') | Out-Null
        $tools.Add('actionlint') | Out-Null
    }
    if (@($files | Where-Object { $_.StartsWith('scripts/', (Get-GameWipPathComparison)) -or $_.StartsWith('.github/scripts/', (Get-GameWipPathComparison)) }).Count -ne 0)
    {
        $checks.Add('helper standardization') | Out-Null
    }
    if (@($files | Where-Object { $_.StartsWith('.github/scripts/', (Get-GameWipPathComparison)) -and [IO.Path]::GetExtension($_).ToLowerInvariant() -eq '.py' }).Count -ne 0)
    {
        $checks.Add('repository standards tests') | Out-Null
    }

    # Ownership and baseline standards protect every maintained-file change.
    $checks.Add('quality ownership') | Out-Null
    $checks.Add('repository standards') | Out-Null
    $tools.Add('python') | Out-Null

    return [pscustomobject]@{
        Checks = @($checks | Sort-Object -Unique)
        Tools = @($tools | Sort-Object -Unique)
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
    param(
        [switch]$Changed,
        [string]$ChangedFilesPath
    )

    if ($Changed -and -not [string]::IsNullOrWhiteSpace($ChangedFilesPath))
    {
        throw 'Use either -ChangedOnly or -ChangedFilesPath, not both.'
    }

    $manifestProvided = -not [string]::IsNullOrWhiteSpace($ChangedFilesPath)

    if (-not $Changed -and -not $manifestProvided)
    {
        return [pscustomobject]@{
            Requested = $false
            UseChanged = $false
            Expanded = $false
            Files = @()
        }
    }

    if ($manifestProvided)
    {
        $manifestFullPath = [IO.Path]::GetFullPath($ChangedFilesPath)

        if (-not (Test-Path -LiteralPath $manifestFullPath -PathType Leaf))
        {
            throw "Changed-file manifest was not found: $ChangedFilesPath"
        }

        try
        {
            $manifest = [IO.File]::ReadAllText($manifestFullPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
        }
        catch
        {
            throw "Could not read the changed-file manifest: $ChangedFilesPath"
        }

        $changedFilesProperty = if ($null -eq $manifest)
        {
            $null
        }
        else
        {
            $manifest.PSObject.Properties['changed_files']
        }

        if ($null -eq $changedFilesProperty -or $changedFilesProperty.Value -isnot [System.Collections.IEnumerable] -or
            $changedFilesProperty.Value -is [string])
        {
            throw "Changed-file manifest has no 'changed_files' array: $ChangedFilesPath"
        }

        $files = @(
            foreach ($entry in $changedFilesProperty.Value)
            {
                $relativePath = ([string]$entry).Replace('\', '/')

                if ([string]::IsNullOrWhiteSpace($relativePath) -or [IO.Path]::IsPathRooted($relativePath) -or
                    @($relativePath.Split('/')) -contains '..')
                {
                    throw "Changed-file manifest contains an invalid repository path: '$entry'"
                }

                $relativePath
            }
        ) | Sort-Object -Unique
    }
    else
    {
        $files = @(Get-GameWipChangedRepositoryFile)
    }

    $files = @($files | Where-Object { $_ -notmatch '^(?:external|build|install|docs/releases)/' })
    $removedFiles = @($files | Where-Object { -not (Test-Path -LiteralPath (Join-Path $RepositoryRoot $_) -PathType Leaf) })
    $expanded = (Test-GameWipQualityPolicyChange -Files $files) -or $removedFiles.Count -ne 0

    if (-not $expanded)
    {
        $files = @($files | Where-Object { Test-Path -LiteralPath (Join-Path $RepositoryRoot $_) -PathType Leaf })
    }

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
