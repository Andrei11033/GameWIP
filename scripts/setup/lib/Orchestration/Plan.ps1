# ------------------------------------------------------------
# Planning and execution
# ------------------------------------------------------------

function Get-GameWipWindowsHostInstallPlan
{
    if (-not (Test-GameWipWindowsHost))
    {
        return @()
    }

    $packages = @(
        $ProjectTools.tools |
            Where-Object { $_.provider.kind -eq 'winget' } |
            ForEach-Object { "$($_.name) ($($_.provider.package))" }
    )

    if ($packages.Count -eq 0)
    {
        return @()
    }

    return @("Ask before installing missing WinGet host tools outside build/: $($packages -join ', ').")
}

function Get-GameWipEditorInstallPlan
{
    if (Test-GameWipWindowsHost)
    {
        return @(
            'Use existing VS Code; skip its integration if missing.',
            'Ask before installing Visual Studio when selected.'
        )
    }

    return @('Use installed editors and skip integrations for missing editors.')
}

function Get-GameWipSetupPlan
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    switch ($SelectedAction)
    {
        'full'
        {
            $hostPlan = if (Test-GameWipWindowsHost)
            {
                @(Get-GameWipMsys2SetupPlan -Update)
            }
            else
            {
                @(Get-GameWipSetupLinuxPlan)
            }
            $commonPlan = @(
                'Install or repair declared project tools.',
                'Prepare the repository and development configuration.',
                'Prepare or reuse the locked dependency cache.'
            )
            $commonPlan += @(Get-GameWipWindowsHostInstallPlan)
            $commonPlan += @(Get-GameWipEditorInstallPlan)
            $commonPlan += 'Build the matching Tracy profiler.'

            $commonPlan += @('Build docs unless skipped.', 'Verify the selected environment.')
            return @($hostPlan) + @($commonPlan)
        }

        'repair'
        {
            if (Test-GameWipWindowsHost)
            {
                return @(Get-GameWipMsys2SetupPlan) +
                    @('Repair declared project tools.') +
                    @(Get-GameWipWindowsHostInstallPlan) +
                    @('Prepare or reuse the locked dependency cache.') +
                    @(Get-GameWipEditorInstallPlan) +
                    @('Build the matching Tracy profiler.') +
                    @('Verify complete environment.')
            }

            return @(Get-GameWipSetupLinuxPlan) +
                @('Repair declared project tools.', 'Prepare or reuse the locked dependency cache.') +
                @(Get-GameWipEditorInstallPlan) +
                @('Build the matching Tracy profiler.') +
                @('Verify the selected environment.')
        }

        'update'
        {
            if (Test-GameWipWindowsHost)
            {
                return @(Get-GameWipMsys2SetupPlan -Update) +
                    @('Install or update declared project tools.') +
                    @(Get-GameWipWindowsHostInstallPlan) +
                    @('Fast-forward repository.', 'Prepare or refresh dependencies for the updated lock file.') +
                    @(Get-GameWipEditorInstallPlan) +
                    @('Build the matching Tracy profiler.') +
                    @('Verify complete environment.')
            }

            return @(Get-GameWipSetupLinuxPlan) +
                @('Update declared project tools.', 'Fast-forward repository.', 'Prepare or refresh dependencies for the updated lock file.') +
                @(Get-GameWipEditorInstallPlan) +
                @('Build the matching Tracy profiler.') +
                @('Verify the selected environment.')
        }

        'deps'
        {
            return @(Get-GameWipDependencyPreparationPlan)
        }

        'tool'
        {
            $toolPlan = @('Install, update, or repair tools at versions already declared by the checkout.', 'Do not modify project pins.')

            if (Test-GameWipWindowsHost)
            {
                return @(Get-GameWipMsys2SetupPlan) + $toolPlan + @(Get-GameWipWindowsHostInstallPlan)
            }

            return $toolPlan
        }

        'msys2'
        {
            return @(Get-GameWipMsys2SetupPlan)
        }

        'system'
        {
            return @(Get-GameWipSetupLinuxPlan)
        }

        'repo'
        {
            return @('Prepare the Git checkout and development configuration.')
        }

        'editor'
        {
            return @(Get-GameWipEditorInstallPlan)
        }

        'vs'
        {
            $visualStudio = $EditorConfig.Options | Where-Object Handler -eq 'visual-studio' | Select-Object -First 1
            return @(
                'Use an existing Visual Studio installation and apply .vsconfig.',
                "If Visual Studio is missing, ask before installing $($visualStudio.Package) outside build/."
            )
        }

        'profiler'
        {
            $hostPlan = if (Test-GameWipWindowsHost)
            {
                @()
            }
            else
            {
                @(Get-GameWipSetupLinuxPlan)
            }

            return @($hostPlan) + @('Prepare or reuse the locked Tracy source, then build/install the matching profiler.')
        }

        'doc'
        {
            return @(
                'Configure and build the documentation preset.',
                'Verify generated documentation.',
                'Open the manual only after a real interactive build.'
            )
        }

        default
        {
            return @()
        }
    }
}
