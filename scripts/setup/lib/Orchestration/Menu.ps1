# ------------------------------------------------------------
# Interactive menu
# ------------------------------------------------------------

function Show-GameWipSetupMenu
{
    Assert-GameWipSetupHost
    Assert-GameWipSetupRepository -RepositoryRoot $RepositoryRoot
    Assert-GameWipSetupActionCatalog

    while ($true)
    {
        $menuActions = @(Get-GameWipAvailableSetupActions | Where-Object { $_.ContainsKey('Key') })
        $menuItems = @($menuActions | ForEach-Object {
                [pscustomobject]@{
                    Key = [string]$_.Key
                    Label = [string]$_.Name
                    Handler = [string]$_.Id
                }
            })
        $choice = Read-GameWipActionMenuItem `
            -Title 'GameWIP Development Environment' `
            -Prompt 'Choose an action:' `
            -Items $menuItems `
            -ExitLabel Exit

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        Invoke-GameWipSetupOperation -SelectedAction ([string]$choice.Value) | Out-Null
    }
}
