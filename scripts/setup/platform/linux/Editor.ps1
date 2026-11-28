# Linux editor paths.

# ------------------------------------------------------------
# Visual Studio Code user and extension directories
# ------------------------------------------------------------

function Get-GameWipVsCodeUserDirectory
{
    $configRoot = if (-not [string]::IsNullOrWhiteSpace($env:XDG_CONFIG_HOME))
    {
        $env:XDG_CONFIG_HOME
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:HOME))
    {
        Join-Path $env:HOME '.config'
    }
    else
    {
        throw 'HOME and XDG_CONFIG_HOME are unavailable; cannot locate the Visual Studio Code user directory.'
    }

    return Join-Path $configRoot 'Code/User'
}

function Get-GameWipVsCodeExtensionRoot
{
    if ([string]::IsNullOrWhiteSpace($env:HOME))
    {
        throw 'HOME is unavailable; cannot locate Visual Studio Code extensions.'
    }

    return Join-Path $env:HOME '.vscode/extensions'
}
