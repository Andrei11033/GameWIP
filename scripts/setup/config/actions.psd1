@{
    Actions = @(
        @{ Id = 'full'; Key = '1'; Name = 'Complete setup (recommended)'; Description = 'Install or repair every required component and verify the checkout.'; MachineChanges = $true }
        @{ Id = 'check'; Key = '2'; Name = 'Check environment (read only)'; Description = 'Verify required tools and project state without changing them.'; MachineChanges = $false }
        @{ Id = 'update'; Key = '3'; Name = 'Update environment'; Description = 'Update compatible tools, integrations, and the checkout, then verify.'; MachineChanges = $true }
        @{ Id = 'repair'; Key = '4'; Name = 'Repair environment'; Description = 'Reapply required state without ordinary upgrades.'; MachineChanges = $true }
        @{ Id = 'editor'; Key = '5'; Name = 'Choose and configure editors/IDEs'; Description = 'Choose editors and install their GameWIP integrations.'; MachineChanges = $true }
        @{ Id = 'msys2'; Key = '6'; Name = 'Configure MSYS2'; Description = 'Install or repair declared UCRT64 and CLANG64 packages.'; MachineChanges = $true }
        @{ Id = 'repository'; Key = '7'; Name = 'Prepare repository'; Description = 'Initialize pinned submodules and configure the development tree.'; MachineChanges = $true }
        @{ Id = 'tools'; Key = '8'; Name = 'Install common machine tools'; Description = 'Install declared WinGet-managed command-line tools.'; MachineChanges = $true }
        @{ Id = 'docs'; Key = '9'; Name = 'Build documentation'; Description = 'Build, verify, and optionally open the generated manual.'; MachineChanges = $false }
        @{ Id = 'profiler'; Key = '0'; Name = 'Install matching Tracy profiler tools'; Description = 'Build and install tools matching the pinned Tracy client.'; MachineChanges = $true }
        @{ Id = 'uninstall'; Key = 'U'; Name = 'Uninstall everything installed by GameWIP'; Description = 'Remove setup-owned software, integrations, and generated setup artifacts.'; MachineChanges = $true }
        @{ Id = 'visual-studio'; Name = 'Install or repair Visual Studio Community'; Description = 'Apply the repository Visual Studio configuration.'; MachineChanges = $true }
        @{ Id = 'list'; Name = 'List setup actions'; Description = 'List supported setup actions and their behavior.'; MachineChanges = $false }
        @{ Id = 'help'; Name = 'Show setup help'; Description = 'Print command-line usage and common options.'; MachineChanges = $false }
        @{ Id = 'menu'; Name = 'Open the interactive setup menu'; Description = 'Open the persistent one-key setup menu.'; MachineChanges = $false }
    )
}
