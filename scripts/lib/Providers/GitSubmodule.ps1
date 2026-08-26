# GameWIP GitSubmodule tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipGitSubmoduleToolLatestVersion
{
    param([hashtable]$Tool)
    $null = $Tool
    return $null
}
function Install-GameWipGitSubmoduleTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    throw "'$($Tool.id)' is updated through the repository's dependency mechanism."
}
