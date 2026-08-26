# GameWIP Unicode data status, reproducibility verification, and intentional regeneration.

Set-StrictMode -Version Latest

function Write-GameWipUnicodeState
{
    param([Parameter(Mandatory = $true)][string]$State, [Parameter(Mandatory = $true)][string]$Label, [string]$Detail = '')
    $normalizedState = $State.ToLowerInvariant()
    $semantic = switch ($normalizedState)
    {
        'pass'
        {
            'Success'
        } 'ready'
        {
            'Success'
        } 'unchanged'
        {
            'Success'
        } 'cached'
        {
            'Accent'
        } 'downloaded'
        {
            'Accent'
        } 'updated'
        {
            'Warning'
        } 'missing'
        {
            'Warning'
        } 'fail'
        {
            'Failure'
        } default
        {
            'Muted'
        }
    }

    Write-GameWipStatusLine `
        -Status $normalizedState `
        -Text $(if ([string]::IsNullOrWhiteSpace($Detail))
        {
            $Label
        }
        else
        {
            "${Label}:"
        }) `
        -Suffix $(if ([string]::IsNullOrWhiteSpace($Detail))
        {
            ''
        }
        else
        {
            $Detail
        }) `
        -Semantic $semantic `
        -SuffixSemantic Muted `
        -Indent 2 `
        -MarkerWidth 12
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
    return [pscustomobject]@{
        Version = $version; VersionRoot = $versionRoot; Archive = Join-Path $versionRoot 'UCD.zip'; UcdRoot = Join-Path $versionRoot 'ucd'; GeneratedRoot = Join-Path $versionRoot 'generated';
        TemporaryHeader = Join-Path $versionRoot 'generated\unicode_properties.h'; Generator = Resolve-GameWipRepositoryPath -Path ([string]$unicodeConfig.Generator);
        CheckedInHeader = Resolve-GameWipRepositoryPath -Path ([string]$unicodeConfig.GeneratedHeader); FormatConfig = Resolve-GameWipRepositoryPath -Path '.clang-format';
        Url = ([string]$unicodeConfig.UcdUrlTemplate -f $version); RequiredFiles = @($unicodeConfig.RequiredFiles)
    }
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
    Write-Host "  Standard:     Unicode $($paths.Version)"
    Write-Host '  Cache:        ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$paths.VersionRoot) -Semantic Muted
    Write-Host '  Generator:    ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$paths.Generator) -Semantic Muted
    Write-Host '  Runtime data: ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$paths.CheckedInHeader) -Semantic Muted
    try
    {
        $python = Resolve-GameWipPython; Write-GameWipUnicodeState -State ready -Label Python -Detail "$($python.Version) via $($python.Source) [$($python.Path)]"
    }
    catch
    {
        Write-GameWipUnicodeState -State missing -Label Python -Detail $_.Exception.Message
    }
    try
    {
        $formatter = Resolve-GameWipClangFormat; Write-GameWipUnicodeState -State ready -Label clang-format -Detail "$($formatter.Version) via $($formatter.Source) [$($formatter.Path)]"
    }
    catch
    {
        Write-GameWipUnicodeState -State missing -Label clang-format -Detail $_.Exception.Message
    }
    Write-GameWipUnicodeState -State $(if (Test-Path -LiteralPath $paths.Archive)
        {
            'cached'
        }
        else
        {
            'missing'
        }) -Label 'UCD archive' -Detail $paths.Archive
    foreach ($relativePath in $paths.RequiredFiles)
    {
        Write-GameWipUnicodeState -State $(if (Test-Path -LiteralPath (Join-Path $paths.UcdRoot $relativePath))
            {
                'ready'
            }
            else
            {
                'missing'
            }) -Label $relativePath
    }
    if (Test-Path -LiteralPath $paths.CheckedInHeader)
    {
        Write-GameWipUnicodeState -State ready -Label 'Checked-in property table' -Detail "sha256=$(Get-GameWipFileSha256 -Path $paths.CheckedInHeader)"
    }
    else
    {
        Write-GameWipUnicodeState -State missing -Label 'Checked-in property table' -Detail $paths.CheckedInHeader
    }
}

function Initialize-GameWipUnicodeData
{
    param([Parameter(Mandatory = $true)]$Paths, [switch]$Refresh)
    if (-not $Refresh -and (Test-GameWipUnicodeInput -Paths $Paths))
    {
        Write-GameWipUnicodeState -State cached -Label "Unicode $($Paths.Version) source data" -Detail $Paths.UcdRoot; return
    }
    New-Item -ItemType Directory -Force -Path $Paths.VersionRoot | Out-Null
    if ($Refresh -or -not (Test-Path -LiteralPath $Paths.Archive))
    {
        Write-GameWipOperationEvent -Phase execute -Step unicode-download -Severity progress -Message "Downloading $($Paths.Url)"
        $query = Invoke-GameWipDownload -Uri $Paths.Url -OutFile $Paths.Archive -Label "Unicode $($Paths.Version) UCD"
        if ($query.State -ne 'resolved')
        {
            throw "Unicode data download failed after $($query.Attempts) attempt(s): $($query.Reason)"
        }
        Write-GameWipUnicodeState -State downloaded -Label 'UCD archive' -Detail $Paths.Archive
    }
    else
    {
        Write-GameWipUnicodeState -State cached -Label 'UCD archive' -Detail $Paths.Archive
    }
    if (Test-Path -LiteralPath $Paths.UcdRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $Paths.UcdRoot -OwnedRoot $Paths.VersionRoot
    }
    New-Item -ItemType Directory -Force -Path $Paths.UcdRoot | Out-Null
    Expand-Archive -LiteralPath $Paths.Archive -DestinationPath $Paths.UcdRoot -Force
    $missing = @($Paths.RequiredFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Paths.UcdRoot $_)) })
    if ($missing.Count -ne 0)
    {
        throw "Unicode $($Paths.Version) archive is missing required file(s): $($missing -join ', ')."
    }
}

function Invoke-GameWipUnicodeFormatter
{
    param([Parameter(Mandatory = $true)]$Paths, [Parameter(Mandatory = $true)][string]$InputPath)
    if (-not (Test-Path -LiteralPath $Paths.FormatConfig))
    {
        throw "Repository clang-format configuration is missing: $($Paths.FormatConfig)"
    }
    $formatter = Resolve-GameWipClangFormat
    Invoke-GameWipNative -Name "unicode-format-$($Paths.Version)" -FilePath $formatter.Path -Arguments @("--style=file:$($Paths.FormatConfig)", '--Werror', '--fail-on-incomplete-format', '-i', $InputPath)
}

function Invoke-GameWipUnicodeGenerator
{
    param([Parameter(Mandatory = $true)]$Paths, [Parameter(Mandatory = $true)][string]$OutputPath)
    if (-not (Test-Path -LiteralPath $Paths.Generator))
    {
        throw "Unicode generator is missing: $($Paths.Generator)"
    }
    $python = Resolve-GameWipPython
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
    Remove-Item -LiteralPath $OutputPath -Force -ErrorAction SilentlyContinue
    Invoke-GameWipNative -Name "unicode-generate-$($Paths.Version)" -FilePath $python.Path -Arguments @($Paths.Generator, '--ucd-dir', $Paths.UcdRoot, '--output', $OutputPath)
    if (-not (Test-Path -LiteralPath $OutputPath))
    {
        throw "Unicode generator completed without producing '$OutputPath'."
    }
    Invoke-GameWipUnicodeFormatter -Paths $Paths -InputPath $OutputPath
}

function Test-GameWipFilesEqual
{
    param([Parameter(Mandatory = $true)][string]$First, [Parameter(Mandatory = $true)][string]$Second)
    return [Collections.StructuralComparisons]::StructuralEqualityComparer.Equals([IO.File]::ReadAllBytes($First), [IO.File]::ReadAllBytes($Second))
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
    $checkedHash = Get-GameWipFileSha256 -Path $paths.CheckedInHeader
    $generatedHash = Get-GameWipFileSha256 -Path $paths.TemporaryHeader
    Write-Host "Checked-in SHA-256: $checkedHash"
    Write-Host "Generated  SHA-256: $generatedHash"
    if (-not (Test-GameWipFilesEqual -First $paths.CheckedInHeader -Second $paths.TemporaryHeader))
    {
        throw (New-GameWipDiagnosticException -Code 'unicode-reproducibility' -Summary "Unicode $($paths.Version) generated data differs from the tracked table." -Details "Generated candidate: $($paths.TemporaryHeader)" -SuggestedActions @('Review the generated candidate.', "Regenerate intentionally with '.\gamewip.bat unicode regenerate'."))
    }
    Write-GameWipUnicodeState -State pass -Label Reproducibility -Detail 'official versioned UCD input reproduces the checked-in table exactly'
}

function Invoke-GameWipUnicodeRegenerate
{
    $paths = Get-GameWipUnicodePath
    Write-GameWipSection "Unicode $($paths.Version) table regeneration"
    Initialize-GameWipUnicodeData -Paths $paths -Refresh:$RefreshUnicodeData
    Invoke-GameWipUnicodeGenerator -Paths $paths -OutputPath $paths.TemporaryHeader
    $beforeHash = if (Test-Path -LiteralPath $paths.CheckedInHeader)
    {
        Get-GameWipFileSha256 -Path $paths.CheckedInHeader
    }
    else
    {
        '<missing>'
    }
    $afterHash = Get-GameWipFileSha256 -Path $paths.TemporaryHeader
    if ((Test-Path -LiteralPath $paths.CheckedInHeader) -and (Test-GameWipFilesEqual -First $paths.CheckedInHeader -Second $paths.TemporaryHeader))
    {
        Write-GameWipUnicodeState -State unchanged -Label 'Property table' -Detail "sha256=$afterHash"; return
    }
    Write-GameWipTextAtomic -Path $paths.CheckedInHeader -Content ([IO.File]::ReadAllText($paths.TemporaryHeader))
    Add-GameWipOperationChange -Message 'Updated foundation/unicode/internal/generated/unicode_properties.h'
    Write-GameWipUnicodeState -State updated -Label 'Property table' -Detail $paths.CheckedInHeader
    Write-Host "Previous SHA-256: $beforeHash"
    Write-Host "Current  SHA-256: $afterHash"
}

function Invoke-GameWipUnicodeAction
{
    param([Parameter(Mandatory = $true)][ValidateSet('status', 'verify', 'regenerate')][string]$Name)
    switch ($Name)
    {
        status
        {
            Show-GameWipUnicodeStatus
        } verify
        {
            Invoke-GameWipUnicodeVerify
        } regenerate
        {
            Invoke-GameWipUnicodeRegenerate
        }
    }
}
