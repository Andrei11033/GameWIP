# GameWIP setup documentation build and warning-log verification.

Set-StrictMode -Version Latest

function Invoke-GameWipDocumentationBuild
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [switch]$Open
    )

    $oldPath = $env:Path
    Push-Location $RepositoryRoot
    try
    {
        $ucrtBin = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin'
        $env:Path = @($ucrtBin, $oldPath) -join [IO.Path]::PathSeparator
        Invoke-GameWipSetupNative -FilePath 'cmake' -ArgumentList @('--preset', 'docs') | Out-Null
        Invoke-GameWipSetupNative -FilePath 'cmake' -ArgumentList @('--build', '--preset', 'docs', '--parallel') | Out-Null

        $warningLog = Join-Path $RepositoryRoot 'build\docs\docs\doxygen\doxygen_warnings.log'
        if (-not (Test-Path -LiteralPath $warningLog))
        {
            throw 'Doxygen did not produce its warning log.'
        }
        if ((Get-Item -LiteralPath $warningLog).Length -ne 0)
        {
            Get-Content -LiteralPath $warningLog
            throw 'Doxygen emitted warnings.'
        }
        $index = Join-Path $RepositoryRoot 'build\docs\docs\doxygen\html\index.html'
        Write-Host "  Ready: $index"
        if ($Open)
        {
            Start-Process -FilePath $index
            Write-Host '  Opened the generated manual in the default browser.'
        }
    }
    finally
    {
        $env:Path = $oldPath
        Pop-Location
    }
}
