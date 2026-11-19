# External tool-provider support for the project helper.

# ------------------------------------------------------------
# External ownership boundary
# ------------------------------------------------------------

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

    if ($Tool.provider.Contains('bootstrap'))
    {
        switch ([string]$Tool.provider.bootstrap)
        {
            'msys2BaseArchive'
            {
                Install-GameWipMsys2BaseArchive -MsysRoot (Get-GameWipMsys2Root)
                return
            }
        }
    }

    throw "'$($Tool.id)' is managed by its specialized external workflow."
}
