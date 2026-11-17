# Setup documentation build and warning-log verification.

# ------------------------------------------------------------
# Documentation build orchestration
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Invoke-GameWipDocumentationBuild
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [switch]$Open
    )

    $buildDirectory = Get-GameWipBuildDirectory -PresetName 'docs'
    Push-Location $RepositoryRoot

    try
    {
        Invoke-GameWipConfigurePreset -Name 'docs' | Out-Null
        Invoke-GameWipBuildTarget -Name 'docs' -Target 'docs' | Out-Null

        $documentationRoot = [IO.Path]::Combine([string[]]@($buildDirectory, 'docs', 'doxygen'))
        $warningLog = Join-Path $documentationRoot 'doxygen_warnings.log'

        if (-not (Test-Path -LiteralPath $warningLog))
        {
            throw 'Doxygen did not produce its warning log.'
        }

        if ((Get-Item -LiteralPath $warningLog).Length -ne 0)
        {
            Get-Content -LiteralPath $warningLog
            throw 'Doxygen emitted warnings.'
        }

        $index = Join-Path (Join-Path $documentationRoot 'html') 'index.html'
        Write-Host "  Ready: $index"

        if ($Open)
        {
            Start-Process -FilePath $index
            Write-Host '  Opened the generated manual in the default browser.'
        }
    }

    finally
    {
        Pop-Location
    }
}
