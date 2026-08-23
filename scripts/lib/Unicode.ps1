# GameWIP Unicode helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Write-GameWipUnicodeState
{
    param(
        [Parameter(Mandatory = $true)][string]$State,
        [Parameter(Mandatory = $true)][string]$Label,
        [string]$Detail = ''
    )

    $marker = "[$($State.ToLowerInvariant())]"
    $color = switch ($State.ToLowerInvariant())
    {
        'pass'
        {
            'Green'
        }
        'ready'
        {
            'Green'
        }
        'cached'
        {
            'Cyan'
        }
        'downloaded'
        {
            'Cyan'
        }
        'updated'
        {
            'Yellow'
        }
        'unchanged'
        {
            'Green'
        }
        'missing'
        {
            'Yellow'
        }
        'fail'
        {
            'Red'
        }
        default
        {
            'Gray'
        }
    }
    $suffix = if ([string]::IsNullOrWhiteSpace($Detail))
    {
        ''
    }
    else
    {
        ": $Detail"
    }
    Write-Host ("  {0,-12} {1}{2}" -f $marker, $Label, $suffix) -ForegroundColor $color
}

function Get-GameWipUnicodePath
{
    $unicodeConfig = $CommandConfig.Unicode
    $cacheRootSetting = if (-not [string]::IsNullOrWhiteSpace($UnicodeDataRoot))
    {
        $UnicodeDataRoot
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_UNICODE_DATA_ROOT))
    {
        $env:GAMEWIP_UNICODE_DATA_ROOT
    }
    else
    {
        Join-Path ([string]$ProjectConfig.storage.cache) 'unicode'
    }

    $cacheRoot = Resolve-GameWipRepositoryPath -Path $cacheRootSetting
    $version = [string]$unicodeConfig.Version
    $versionRoot = Join-Path $cacheRoot $version

    [pscustomobject]@{
        Version = $version
        VersionRoot = $versionRoot
        Archive = Join-Path $versionRoot 'UCD.zip'
        UcdRoot = Join-Path $versionRoot 'ucd'
        GeneratedRoot = Join-Path $versionRoot 'generated'
        TemporaryHeader = Join-Path $versionRoot 'generated\unicode_properties.h'
        Generator = Resolve-GameWipRepositoryPath -Path ([string]$unicodeConfig.Generator)
        CheckedInHeader = Resolve-GameWipRepositoryPath -Path ([string]$unicodeConfig.GeneratedHeader)
        FormatConfig = Resolve-GameWipRepositoryPath -Path '.clang-format'
        Url = ([string]$unicodeConfig.UcdUrlTemplate -f $version)
        RequiredFiles = @($unicodeConfig.RequiredFiles)
    }
}

function Invoke-GameWipUnicodeFormatter
{
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][string]$InputPath
    )

    if (-not (Test-Path -LiteralPath $Paths.FormatConfig))
    {
        throw "Repository clang-format configuration is missing: $($Paths.FormatConfig)"
    }

    $formatter = Resolve-GameWipClangFormat
    Write-GameWipUnicodeState -State 'ready' -Label 'clang-format' -Detail "$($formatter.Version) via $($formatter.Source)"
    Invoke-GameWipNative `
        -Name "unicode-format-$($Paths.Version)" `
        -FilePath $formatter.Path `
        -Arguments @("--style=file:$($Paths.FormatConfig)", '--Werror', '--fail-on-incomplete-format', '-i', $InputPath)
    Write-GameWipUnicodeState -State 'pass' -Label 'Generated formatting' -Detail 'repository .clang-format applied'
}

function Test-GameWipUnicodeInput
{
    param([Parameter(Mandatory = $true)]$Paths)

    foreach ($relativePath in $Paths.RequiredFiles)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $Paths.UcdRoot $relativePath)))
        {
            return $false
        }
    }
    return $true
}

function Show-GameWipUnicodeStatus
{
    $paths = Get-GameWipUnicodePath
    Write-GameWipSection 'Unicode data status'
    Write-Host "  Standard:    Unicode $($paths.Version)"
    Write-Host "  Cache:       $($paths.VersionRoot)"
    Write-Host "  Generator:   $($paths.Generator)"
    Write-Host "  Runtime data: $($paths.CheckedInHeader)"

    try
    {
        $python = Resolve-GameWipPython
        Write-GameWipUnicodeState -State 'ready' -Label 'Python' -Detail "$($python.Version) via $($python.Source) [$($python.Path)]"
    }
    catch
    {
        Write-GameWipUnicodeState -State 'missing' -Label 'Python' -Detail $_.Exception.Message
    }

    try
    {
        $formatter = Resolve-GameWipClangFormat
        Write-GameWipUnicodeState -State 'ready' -Label 'clang-format' -Detail "$($formatter.Version) via $($formatter.Source) [$($formatter.Path)]"
    }
    catch
    {
        Write-GameWipUnicodeState -State 'missing' -Label 'clang-format' -Detail $_.Exception.Message
    }

    if (Test-Path -LiteralPath $paths.FormatConfig)
    {
        Write-GameWipUnicodeState -State 'ready' -Label 'Format config' -Detail $paths.FormatConfig
    }
    else
    {
        Write-GameWipUnicodeState -State 'missing' -Label 'Format config' -Detail $paths.FormatConfig
    }

    if (Test-Path -LiteralPath $paths.Archive)
    {
        Write-GameWipUnicodeState -State 'cached' -Label 'UCD archive' -Detail $paths.Archive
    }
    else
    {
        Write-GameWipUnicodeState -State 'missing' -Label 'UCD archive' -Detail 'downloaded automatically by verify/regenerate'
    }

    foreach ($relativePath in $paths.RequiredFiles)
    {
        $inputPath = Join-Path $paths.UcdRoot $relativePath
        if (Test-Path -LiteralPath $inputPath)
        {
            Write-GameWipUnicodeState -State 'ready' -Label $relativePath
        }
        else
        {
            Write-GameWipUnicodeState -State 'missing' -Label $relativePath
        }
    }

    if (Test-Path -LiteralPath $paths.CheckedInHeader)
    {
        $hash = (Get-FileHash -LiteralPath $paths.CheckedInHeader -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-GameWipUnicodeState -State 'ready' -Label 'Checked-in property table' -Detail "sha256=$hash"
    }
    else
    {
        Write-GameWipUnicodeState -State 'missing' -Label 'Checked-in property table' -Detail $paths.CheckedInHeader
    }
}

function Initialize-GameWipUnicodeData
{
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [switch]$Refresh
    )

    if (-not $Refresh -and (Test-GameWipUnicodeInput -Paths $Paths))
    {
        Write-GameWipUnicodeState -State 'cached' -Label "Unicode $($Paths.Version) source data" -Detail $Paths.UcdRoot
        return
    }

    New-Item -ItemType Directory -Force -Path $Paths.VersionRoot | Out-Null
    if ($Refresh -or -not (Test-Path -LiteralPath $Paths.Archive))
    {
        Write-Host "  Downloading: $($Paths.Url)"
        $temporaryArchive = "$($Paths.Archive).download"
        Remove-Item -LiteralPath $temporaryArchive -Force -ErrorAction SilentlyContinue
        try
        {
            Invoke-WebRequest -Uri $Paths.Url -OutFile $temporaryArchive -UseBasicParsing
            Move-Item -LiteralPath $temporaryArchive -Destination $Paths.Archive -Force
        }
        finally
        {
            Remove-Item -LiteralPath $temporaryArchive -Force -ErrorAction SilentlyContinue
        }
        Write-GameWipUnicodeState -State 'downloaded' -Label 'UCD archive' -Detail $Paths.Archive
    }
    else
    {
        Write-GameWipUnicodeState -State 'cached' -Label 'UCD archive' -Detail $Paths.Archive
    }

    if (Test-Path -LiteralPath $Paths.UcdRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $Paths.UcdRoot -OwnedRoot $Paths.VersionRoot
    }
    New-Item -ItemType Directory -Force -Path $Paths.UcdRoot | Out-Null
    Expand-Archive -LiteralPath $Paths.Archive -DestinationPath $Paths.UcdRoot -Force

    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($relativePath in $Paths.RequiredFiles)
    {
        $inputPath = Join-Path $Paths.UcdRoot $relativePath
        if (Test-Path -LiteralPath $inputPath)
        {
            Write-GameWipUnicodeState -State 'ready' -Label $relativePath
        }
        else
        {
            Write-GameWipUnicodeState -State 'missing' -Label $relativePath
            $missing.Add($relativePath) | Out-Null
        }
    }
    if ($missing.Count -ne 0)
    {
        throw "Unicode $($Paths.Version) archive is missing $($missing.Count) required data file(s). Delete '$($Paths.VersionRoot)' and retry with -RefreshUnicodeData."
    }
}

function Invoke-GameWipUnicodeGenerator
{
    param(
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    if (-not (Test-Path -LiteralPath $Paths.Generator))
    {
        throw "Unicode generator is missing: $($Paths.Generator)"
    }

    $python = Resolve-GameWipPython
    $outputDirectory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    Remove-Item -LiteralPath $OutputPath -Force -ErrorAction SilentlyContinue

    Write-GameWipUnicodeState -State 'ready' -Label 'Python' -Detail "$($python.Version) via $($python.Source)"
    Invoke-GameWipNative `
        -Name "unicode-generate-$($Paths.Version)" `
        -FilePath $python.Path `
        -Arguments @($Paths.Generator, '--ucd-dir', $Paths.UcdRoot, '--output', $OutputPath)

    if (-not (Test-Path -LiteralPath $OutputPath))
    {
        throw "Unicode generator completed without producing '$OutputPath'."
    }

    Invoke-GameWipUnicodeFormatter -Paths $Paths -InputPath $OutputPath
}

function Test-GameWipFilesEqual
{
    param(
        [Parameter(Mandatory = $true)][string]$First,
        [Parameter(Mandatory = $true)][string]$Second
    )

    $firstBytes = [IO.File]::ReadAllBytes($First)
    $secondBytes = [IO.File]::ReadAllBytes($Second)
    [System.Collections.StructuralComparisons]::StructuralEqualityComparer.Equals($firstBytes, $secondBytes)
}

function Invoke-GameWipUnicodeVerify
{
    $paths = Get-GameWipUnicodePath
    Write-GameWipSection "Unicode $($paths.Version) reproducibility verification"
    Initialize-GameWipUnicodeData -Paths $paths -Refresh:$RefreshUnicodeData

    if (-not (Test-Path -LiteralPath $paths.CheckedInHeader))
    {
        throw "Checked-in Unicode property table is missing: $($paths.CheckedInHeader)"
    }

    Invoke-GameWipUnicodeGenerator -Paths $paths -OutputPath $paths.TemporaryHeader
    $checkedHash = (Get-FileHash -LiteralPath $paths.CheckedInHeader -Algorithm SHA256).Hash.ToLowerInvariant()
    $generatedHash = (Get-FileHash -LiteralPath $paths.TemporaryHeader -Algorithm SHA256).Hash.ToLowerInvariant()

    Write-GameWipSection 'Unicode generation result'
    Write-Host "  Checked-in SHA-256: $checkedHash"
    Write-Host "  Generated  SHA-256: $generatedHash"
    if (-not (Test-GameWipFilesEqual -First $paths.CheckedInHeader -Second $paths.TemporaryHeader))
    {
        Write-GameWipUnicodeState -State 'fail' -Label 'Reproducibility' -Detail 'generated output differs from the checked-in property table'
        Write-Host "  Generated candidate retained at: $($paths.TemporaryHeader)"
        throw "Unicode $($paths.Version) generated data does not match the checked-in table. Review the candidate, then regenerate intentionally with '.\gamewip.bat unicode -UnicodeAction regenerate'."
    }

    Write-GameWipUnicodeState -State 'pass' -Label 'Reproducibility' -Detail 'official versioned UCD input reproduces the checked-in table exactly'
}

function Invoke-GameWipUnicodeRegenerate
{
    $paths = Get-GameWipUnicodePath
    Write-GameWipSection "Unicode $($paths.Version) table regeneration"
    Initialize-GameWipUnicodeData -Paths $paths -Refresh:$RefreshUnicodeData
    Invoke-GameWipUnicodeGenerator -Paths $paths -OutputPath $paths.TemporaryHeader

    $beforeHash = if (Test-Path -LiteralPath $paths.CheckedInHeader)
    {
        (Get-FileHash -LiteralPath $paths.CheckedInHeader -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    else
    {
        '<missing>'
    }
    $afterHash = (Get-FileHash -LiteralPath $paths.TemporaryHeader -Algorithm SHA256).Hash.ToLowerInvariant()

    if ((Test-Path -LiteralPath $paths.CheckedInHeader) -and (Test-GameWipFilesEqual -First $paths.CheckedInHeader -Second $paths.TemporaryHeader))
    {
        Write-GameWipUnicodeState -State 'unchanged' -Label 'Property table' -Detail "sha256=$afterHash"
        return
    }

    $destinationDirectory = Split-Path -Parent $paths.CheckedInHeader
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
    Copy-Item -LiteralPath $paths.TemporaryHeader -Destination $paths.CheckedInHeader -Force
    Write-GameWipUnicodeState -State 'updated' -Label 'Property table' -Detail $paths.CheckedInHeader
    Write-Host "  Previous SHA-256: $beforeHash"
    Write-Host "  Current  SHA-256: $afterHash"
    Write-GameWipNextStepHint 'review the generated change with: git diff -- foundation/unicode/internal/generated/unicode_properties.h'
}

function Show-GameWipUnicodeMenu
{
    while ($true)
    {
        Write-Host ''
        Write-Host 'Unicode Data Maintenance'
        Write-Host '========================'
        Write-Host '1. Show Unicode data status'
        Write-Host '2. Verify checked-in data against official Unicode input'
        Write-Host '3. Regenerate the checked-in property table'
        Write-Host 'ESC. Back'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::ESCape)
        {
            Write-Host 'ESC'; return
        }
        Write-Host $key.KeyChar
        switch ($key.KeyChar)
        {
            '1'
            {
                Show-GameWipUnicodeStatus
            }
            '2'
            {
                Invoke-GameWipUnicodeVerify
            }
            '3'
            {
                if (Read-GameWipYesNo -Prompt 'Regenerate the tracked Unicode property table?' -Default $false)
                {
                    Invoke-GameWipUnicodeRegenerate
                }
            }
            default
            {
                Write-Host 'Press 1-3 or ESC.' -ForegroundColor Yellow
            }
        }
    }
}

function Invoke-GameWipUnicodeAction
{
    param([Parameter(Mandatory = $true)][string]$Name)

    switch ($Name)
    {
        'menu'
        {
            Show-GameWipUnicodeMenu
        }
        'status'
        {
            Show-GameWipUnicodeStatus
        }
        'verify'
        {
            Invoke-GameWipUnicodeVerify
        }
        'regenerate'
        {
            Invoke-GameWipUnicodeRegenerate
        }
    }
}
