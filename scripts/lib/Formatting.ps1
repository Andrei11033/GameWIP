# GameWIP Formatting helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Get-GameWipFormatFile
{
    $extensions = @('.cpp', '.h', '.hpp', '.inl')
    $files = New-Object System.Collections.Generic.List[string]

    foreach ($relativeRoot in @($CommandConfig.Formatting.SourceRoots))
    {
        $root = Resolve-GameWipRepositoryPath -Path ([string]$relativeRoot)
        if (-not (Test-Path -LiteralPath $root))
        {
            continue
        }

        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File)
        {
            if ($extensions -contains $file.Extension.ToLowerInvariant())
            {
                $files.Add($file.FullName) | Out-Null
            }
        }
    }

    @($files | Sort-Object -Unique)
}

function Invoke-GameWipFormat
{
    param([Parameter(Mandatory = $true)][ValidateSet('check', 'apply')][string]$Mode)

    $formatter = Resolve-GameWipClangFormat
    $formatConfig = Resolve-GameWipRepositoryPath -Path ([string]$CommandConfig.Formatting.ConfigPath)
    if (-not (Test-Path -LiteralPath $formatConfig))
    {
        throw "Repository clang-format configuration is missing: $formatConfig"
    }

    $files = @(Get-GameWipFormatFile)
    if ($files.Count -eq 0)
    {
        throw 'No GameWIP-owned C/C++ files were found to format.'
    }

    Write-GameWipSection 'C++ formatting'
    Write-Host "  Mode:       $Mode"
    Write-Host "  Formatter:  $($formatter.Version)"
    Write-Host "  Source:     $($formatter.Source)"
    Write-Host "  Style:      $formatConfig"
    Write-Host "  Files:      $($files.Count)"

    $beforeHashes = @{}
    if ($Mode -eq 'apply')
    {
        foreach ($file in $files)
        {
            $beforeHashes[$file] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
        }
    }

    # Keep native command lines comfortably below Windows command-line limits as the repository grows.
    $batchSize = 40
    $batchCount = [int][Math]::Ceiling($files.Count / [double]$batchSize)
    for ($offset = 0; $offset -lt $files.Count; $offset += $batchSize)
    {
        $last = [Math]::Min($offset + $batchSize - 1, $files.Count - 1)
        $batch = @($files[$offset..$last])
        $arguments = New-Object System.Collections.Generic.List[string]
        $arguments.Add("--style=file:$formatConfig") | Out-Null
        $arguments.Add('--Werror') | Out-Null
        $arguments.Add('--fail-on-incomplete-format') | Out-Null
        if ($Mode -eq 'check')
        {
            $arguments.Add('--dry-run') | Out-Null
        }
        else
        {
            $arguments.Add('-i') | Out-Null
        }
        foreach ($file in $batch)
        {
            $arguments.Add($file) | Out-Null
        }

        $batchNumber = [int]($offset / $batchSize) + 1
        Invoke-GameWipNative `
            -Name "clang-format-$Mode-$batchNumber-of-$batchCount" `
            -FilePath $formatter.Path `
            -Arguments $arguments.ToArray()
    }

    if ($Mode -eq 'check')
    {
        Write-Host "  [pass] All $($files.Count) GameWIP-owned C/C++ files match the repository format." -ForegroundColor Green
        return
    }

    $repositoryPrefix = [IO.Path]::GetFullPath($RepositoryRoot)
    if (-not $repositoryPrefix.EndsWith([IO.Path]::DirectorySeparatorChar.ToString()))
    {
        $repositoryPrefix += [IO.Path]::DirectorySeparatorChar
    }

    $changed = New-Object System.Collections.Generic.List[string]
    foreach ($file in $files)
    {
        $afterHash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
        if ($beforeHashes[$file] -ne $afterHash)
        {
            $fullPath = [IO.Path]::GetFullPath($file)
            $relativePath = $fullPath
            if ($fullPath.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase))
            {
                $relativePath = $fullPath.Substring($repositoryPrefix.Length)
            }
            $changed.Add($relativePath) | Out-Null
        }
    }

    if ($changed.Count -eq 0)
    {
        Write-Host "  [unchanged] All $($files.Count) files were already formatted." -ForegroundColor Green
        return
    }

    Write-Host "  [updated] $($changed.Count) file(s) formatted." -ForegroundColor Yellow
    foreach ($relativePath in $changed)
    {
        Write-Host "    $relativePath"
    }
    Write-GameWipNextStepHint 'review formatting changes with: git diff --check && git diff'
}
