# Windows editor paths.

# ------------------------------------------------------------
# Visual Studio Code user and extension directories
# ------------------------------------------------------------

function Get-GameWipVsCodeUserDirectory
{
    if ([string]::IsNullOrWhiteSpace($env:APPDATA))
    {
        throw 'APPDATA is unavailable; cannot locate the Visual Studio Code user directory.'
    }

    return Join-Path $env:APPDATA 'Code/User'
}

function Get-GameWipVsCodeExtensionRoot
{
    if ([string]::IsNullOrWhiteSpace($env:USERPROFILE))
    {
        throw 'USERPROFILE is unavailable; cannot locate Visual Studio Code extensions.'
    }

    return Join-Path $env:USERPROFILE '.vscode/extensions'
}
