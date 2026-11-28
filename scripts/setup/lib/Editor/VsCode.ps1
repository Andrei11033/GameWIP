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
                $workflowExtensionId = "$($workflowPackage.publisher).$($workflowPackage.name)"
                $workflowExtensionPath = Join-Path $extensionRoot "$workflowExtensionId-$($workflowPackage.version)"

                if (-not (Test-Path -LiteralPath $workflowExtensionPath -PathType Container))
                {
                    $failures.Add("The required GameWIP VS Code workflow extension is missing: $workflowExtensionId@$($workflowPackage.version)")
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
        [Parameter(Mandatory = $true)][string[]]$Extensions,
        [switch]$Update
    )

    $codePath = Resolve-GameWipSetupCommand -Name 'code'

    if ($null -eq $codePath)
    {
        throw 'Visual Studio Code is unavailable; editor integration cannot be applied.'
    }

    $extensionRoot = Get-GameWipVsCodeExtensionRoot
    $installedExtensionDirectories = @(
        Get-ChildItem -LiteralPath $extensionRoot -Directory -ErrorAction SilentlyContinue
    )

    # Install missing marketplace extensions and record new ownership.
    foreach ($extension in $Extensions)
    {
        $installedExtension = $installedExtensionDirectories |
            Where-Object {
                $_.Name -ieq $extension -or
                $_.Name.StartsWith("$extension-", [System.StringComparison]::OrdinalIgnoreCase)
            } |
            Select-Object -First 1
        $installedBefore = $null -ne $installedExtension

        if ($installedBefore -and -not $Update)
        {
            Write-Host "  Ready: Visual Studio Code extension $extension"
            continue
        }

        Write-Host "  Installing Visual Studio Code extension: $extension"
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

    $extensionId = "$($package.publisher).$($package.name)"
    $workflowExtensionPath = Join-Path $extensionRoot "$extensionId-$($package.version)"

    if ($Update -or -not (Test-Path -LiteralPath $workflowExtensionPath -PathType Container))
    {
        $vsixPath = Join-Path $Script:OperationContext.Temp 'gamewip-workflows.vsix'
        Invoke-GameWipVsCodeExtensionPackaging -ExtensionSource $source -Destination $vsixPath
        Write-Host "  Installing Visual Studio Code extension package: $vsixPath"
        Invoke-GameWipSetupNative -FilePath $codePath -ArgumentList @('--install-extension', $vsixPath, '--force') | Out-Null
    }

    # Verify the installed extension version before adding keybindings.
    if (-not (Test-Path -LiteralPath $workflowExtensionPath -PathType Container))
    {
        throw "Visual Studio Code did not install the expected workflow extension: $extensionId@$($package.version)"
    }

    Install-GameWipVsCodeKeybinding -ExtensionSource $source
    Write-Host "  Ready: GameWIP workflow keybindings $($package.version)"
}
