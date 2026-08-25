# GameWIP C/C++ formatting policy and focused changed-file support.

Set-StrictMode -Version Latest

function Get-GameWipFormatFile
{
    param([string[]]$Files = @())

    $extensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl')
    if ($null -ne $Files -and @($Files).Count -ne 0)
    {
        return @(
            $Files |
                ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Where-Object { $extensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() } |
                Sort-Object -Unique
        )
    }

    $result = [System.Collections.Generic.List[string]]::new()
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
                $result.Add($file.FullName) | Out-Null
            }
        }
    }
    return @($result | Sort-Object -Unique)
}

function Invoke-GameWipFormat
{
    param(
        [Parameter(Mandatory = $true)][ValidateSet('check', 'apply')][string]$Mode,
        [string[]]$Files = @()
    )

    $formatter = Resolve-GameWipClangFormat
    $formatConfig = Resolve-GameWipRepositoryPath -Path ([string]$CommandConfig.Formatting.ConfigPath)
    if (-not (Test-Path -LiteralPath $formatConfig))
    {
        throw "Repository clang-format configuration is missing: $formatConfig"
    }

    $formatFiles = @(Get-GameWipFormatFile -Files $Files)
    if ($formatFiles.Count -eq 0)
    {
        if ($null -ne $Files -and @($Files).Count -ne 0)
        {
            Write-Host 'No changed C/C++ files require formatting checks.'; return
        }
        Write-Host '  [SKIP] No C/C++ files are in the selected scope.'; return
    }

    Write-GameWipSection 'C/C++ formatting'
    Write-Host "  Mode:       $Mode"
    Write-Host "  Formatter:  $($formatter.Version)"
    Write-Host "  Source:     $($formatter.Source)"
    Write-Host "  Style:      $formatConfig"
    Write-Host "  Files:      $($formatFiles.Count)"

    $help = Invoke-GameWipProcess -FilePath $formatter.Path -Arguments @('--help') -OutputMode LogOnly -TimeoutSeconds 20
    $supportsIncompleteFormatFailure = $help.ExitCode -eq 0 -and ((@($help.Stdout) + @($help.Stderr)) -join "`n") -match '(?m)^\s*--fail-on-incomplete-format\b'

    $beforeHashes = @{}
    if ($Mode -eq 'apply')
    {
        foreach ($file in $formatFiles)
        {
            $beforeHashes[$file] = Get-GameWipFileSha256 -Path $file
        }
    }

    $batchSize = 40
    $batchCount = [int][Math]::Ceiling($formatFiles.Count / [double]$batchSize)
    for ($offset = 0; $offset -lt $formatFiles.Count; $offset += $batchSize)
    {
        $last = [Math]::Min($offset + $batchSize - 1, $formatFiles.Count - 1)
        $batch = @($formatFiles[$offset..$last])
        $arguments = [System.Collections.Generic.List[string]]::new()
        $arguments.Add("--style=file:$formatConfig") | Out-Null
        $arguments.Add('--Werror') | Out-Null
        if ($supportsIncompleteFormatFailure)
        {
            $arguments.Add('--fail-on-incomplete-format') | Out-Null
        }
        $arguments.Add($(if ($Mode -eq 'check')
                {
                    '--dry-run'
                }
                else
                {
                    '-i'
                })) | Out-Null
        foreach ($file in $batch)
        {
            $arguments.Add($file) | Out-Null
        }
        $batchNumber = [int]($offset / $batchSize) + 1
        Invoke-GameWipNative -Name "clang-format-$Mode-$batchNumber-of-$batchCount" -FilePath $formatter.Path -Arguments $arguments.ToArray() | Out-Null
    }

    if ($Mode -eq 'check')
    {
        Write-GameWipHost "  [pass] All $($formatFiles.Count) selected C/C++ files match the repository format." -ForegroundColor Green
        return
    }

    foreach ($file in $formatFiles)
    {
        $afterHash = Get-GameWipFileSha256 -Path $file
        if ($beforeHashes[$file] -ne $afterHash)
        {
            Add-GameWipOperationChange -Message "Formatted $(Get-GameWipRepositoryRelativePath -Path $file)"
        }
    }
}
