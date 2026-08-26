# GameWIP External tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipExternalToolLatestVersion
{
    param([hashtable]$Tool)
    $null = $Tool
    return $null
}
function Install-GameWipExternalTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    throw "'$($Tool.id)' is managed by its specialized external workflow."
}
