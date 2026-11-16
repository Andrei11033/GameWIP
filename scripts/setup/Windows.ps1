# Compatibility entry point for the former Windows setup path.
& (Join-Path $PSScriptRoot 'Setup.ps1') @args
exit $LASTEXITCODE
