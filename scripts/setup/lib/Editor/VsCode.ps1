# ------------------------------------------------------------
# VS Code integration and extension packaging
# ------------------------------------------------------------

function Get-GameWipEditorFailure
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][hashtable]$EditorConfig,
        [Parameter(Mandatory = $true)][string[]]$SelectedEditors
    )

    $failures = [System.Collections.Generic.List[string]]::new()

    foreach ($id in $SelectedEditors)
    {
        $option = $EditorConfig.Options | Where-Object { $_.Id -eq $id } | Select-Object -First 1

        switch ($option.Handler)
        {
            'vscode'
            {
                # Check the selected editor and required extensions.
                $codePath = Resolve-GameWipSetupCommand -Name $option.Command

                if ($null -eq $codePath)
                {
                    $failures.Add('Selected editor Visual Studio Code is missing.')
                    continue
                }

                $extensionRoot = Get-GameWipVsCodeExtensionRoot

                foreach ($extension in $option.Extensions)
                {
                    $installedExtension = Get-ChildItem -LiteralPath $extensionRoot -Directory -ErrorAction SilentlyContinue |
                        Where-Object { $_.Name -eq $extension -or $_.Name.StartsWith("$extension-") } |
                        Select-Object -First 1

                    if (-not $installedExtension)
                    {
                        $failures.Add("Required Visual Studio Code extension is missing: $extension")
                    }
                }
                $workflowPackagePath = Join-Path $RepositoryRoot 'scripts/setup/editor/gamewip-workflows/package.json'
                $workflowPackage = Get-Content -LiteralPath $workflowPackagePath -Raw | ConvertFrom-Json
                $expectedWorkflowExtension = "$($workflowPackage.publisher).$($workflowPackage.name)@$($workflowPackage.version)"
                $extensionQuery = Invoke-GameWipProcess `
                    -FilePath $codePath `
                    -Arguments @('--list-extensions', '--show-versions') `
                    -OutputMode LogOnly `
                    -TimeoutSeconds 30

                if ($extensionQuery.ExitCode -ne 0)
                {
                    $failures.Add("Visual Studio Code extension discovery failed with exit code $($extensionQuery.ExitCode).")
                    continue
                }

                $installedExtensions = @($extensionQuery.Stdout)

                if ($installedExtensions -notcontains $expectedWorkflowExtension)
                {
                    $failures.Add("The required GameWIP VS Code workflow extension is missing: $expectedWorkflowExtension")
                }

                # Confirm that the managed keybinding block is present.
                $keybindingsPath = Join-Path (Get-GameWipVsCodeUserDirectory) 'keybindings.json'

                if (-not (Test-Path -LiteralPath $keybindingsPath))
                {
                    $failures.Add('The managed GameWIP VS Code keybindings are not installed.')
                }
                else
                {
                    $keybindings = Get-Content -LiteralPath $keybindingsPath -Raw

                    if (-not $keybindings.Contains('// GameWIP managed keybindings begin'))
                    {
                        $failures.Add('The managed GameWIP block is missing from VS Code keybindings.json.')
                    }
                }
            }
            'visual-studio'
            {
                if (-not (Get-GameWipVisualStudioInstance))
                {
                    $failures.Add('Selected IDE Visual Studio with the C++ desktop workload is missing.')
                }
            }
            default
            {
                $failures.Add("Selected editor '$id' has an unsupported setup handler.")
            }
        }
    }

    return $failures
}

function Install-GameWipEditorIntegration
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Extensions
    )

    $codePath = Resolve-GameWipSetupCommand -Name 'code'

    if ($null -eq $codePath)
    {
        throw 'Visual Studio Code is unavailable; editor integration cannot be applied.'
    }

    # Install declared marketplace extensions and record new ownership.
    foreach ($extension in $Extensions)
    {
        Write-Host "  Checking Visual Studio Code extension: $extension"
        $extensionQuery = Invoke-GameWipProcess -FilePath $codePath -Arguments @('--list-extensions') -OutputMode LogOnly -TimeoutSeconds 30

        if ($extensionQuery.ExitCode -ne 0)
        {
            throw "Visual Studio Code extension discovery failed with exit code $($extensionQuery.ExitCode)."
        }

        $installedBefore = @($extensionQuery.Stdout) -contains $extension
        Invoke-GameWipSetupNative -FilePath $codePath -ArgumentList @('--install-extension', $extension, '--force') | Out-Null

        if (-not $installedBefore)
        {
            Add-GameWipOwnedVsCodeExtension -Id $extension
        }
    }

    # Package and install the repository workflow extension.
    $source = Join-Path $RepositoryRoot 'scripts/setup/editor/gamewip-workflows'
    $package = Get-Content -LiteralPath (Join-Path $source 'package.json') -Raw | ConvertFrom-Json

    if ([string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        throw 'Editor integration requires an initialized GameWIP operation-temp directory.'
    }

    $vsixPath = Join-Path $Script:OperationContext.Temp 'gamewip-workflows.vsix'
    Invoke-GameWipVsCodeExtensionPackaging -ExtensionSource $source -Destination $vsixPath
    $extensionId = "$($package.publisher).$($package.name)"
    Write-Host "  Installing Visual Studio Code extension package: $vsixPath"
    Invoke-GameWipSetupNative -FilePath $codePath -ArgumentList @('--install-extension', $vsixPath, '--force') | Out-Null

    # Verify the installed extension version before adding keybindings.
    $extensionQuery = Invoke-GameWipProcess `
        -FilePath $codePath `
        -Arguments @('--list-extensions', '--show-versions') `
        -OutputMode LogOnly `
        -TimeoutSeconds 30

    if ($extensionQuery.ExitCode -ne 0)
    {
        throw "Visual Studio Code extension verification failed with exit code $($extensionQuery.ExitCode)."
    }

    $installedExtensions = @($extensionQuery.Stdout)

    if (-not ($installedExtensions | Where-Object { $_ -eq "$extensionId@$($package.version)" }))
    {
        throw "Visual Studio Code did not report the expected extension after installation: $extensionId@$($package.version)"
    }

    Install-GameWipVsCodeKeybinding -ExtensionSource $source
    Write-Host "  Ready: GameWIP workflow keybindings $($package.version)"
}
