# Root menu dispatch.
# ------------------------------------------------------------
# Main menu
# ------------------------------------------------------------

function Show-GameWipMenu
{
    Assert-GameWipInteractiveConsole -Purpose 'The GameWIP menu'

    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId root -Header { Write-GameWipInteractiveHeader }

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'menu-development'
            {
                Show-GameWipDevelopmentMenu
            }

            'menu-validation'
            {
                Show-GameWipValidationMenu
            }

            'menu-quality'
            {
                Show-GameWipQualityMenu
            }

            'menu-tools'
            {
                Show-GameWipToolsMenu
            }

            'menu-git-workspace'
            {
                Show-GameWipGitWorkspaceMenu
            }

            'menu-advanced'
            {
                Show-GameWipAdvancedMenu
            }

            'help'
            {
                Show-GameWipHelp
                Show-GameWipProjectCatalog
            }
        }
    }
}
