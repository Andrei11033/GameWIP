# GameWIP editor selection, installation, workflow-extension packaging, and advisory state.

Set-StrictMode -Version Latest

function Get-GameWipEditorPreferencePath
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    return Join-Path $RepositoryRoot (Join-Path $ProjectConfig.storage.state 'editor-selection.json')
}

function Get-GameWipEditorSelection
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][hashtable]$EditorConfig
    )

    $preferencePath = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    if (-not (Test-Path -LiteralPath $preferencePath))
    {
        return @($EditorConfig.Default)
    }

    try
    {
        $preference = Get-Content -LiteralPath $preferencePath -Raw | ConvertFrom-Json
        $selected = @($preference.editors)
        if ($selected.Count -eq 0)
        {
            throw 'The advisory editor selection contains no editors.'
        }
        $known = [System.Collections.Generic.HashSet[string]]::new(
            [string[]]@($EditorConfig.Options | ForEach-Object { $_.Id })
        )
        foreach ($id in $selected)
        {
            if (-not $known.Contains($id))
            {
                throw "Unknown editor '$id'."
            }
        }
        return $selected
    }
    catch
    {
        Write-Warning "Ignoring corrupt advisory editor state '$preferencePath': $($_.Exception.Message)"
        return @($EditorConfig.Default)
    }
}

function Save-GameWipEditorSelection
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Editors
    )

    $preferencePath = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $preferencePath) | Out-Null
    [ordered]@{
        schemaVersion = 1
        editors = @($Editors)
    } | ConvertTo-Json | Set-Content -LiteralPath $preferencePath -Encoding UTF8
}

function Select-GameWipEditor
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][hashtable]$EditorConfig
    )

    $selected = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@(Get-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    )
    while ($true)
    {
        Write-Host ''
        Write-Host 'Select editors and IDEs'
        Write-Host '======================='
        Write-Host 'Press a number to toggle an editor:'
        foreach ($option in $EditorConfig.Options)
        {
            $mark = if ($selected.Contains($option.Id))
            {
                'x'
            }
            else
            {
                ' '
            }
            $recommended = if ($option.Recommended)
            {
                ' (recommended)'
            }
            else
            {
                ''
            }
            Write-Host "$($option.Key). [$mark] $($option.Name)$recommended"
        }
        Write-Host 'S. Save selection'
        Write-Host 'Esc. Cancel'
        $allowedKeys = @($EditorConfig.Options | ForEach-Object { [string]$_.Key }) + @('S', 's')
        $choiceResult = Read-GameWipActionKey -Prompt 'Choose:' -AllowedKeys $allowedKeys -AllowEscape
        if ($choiceResult.Status -eq 'Cancelled')
        {
            return $false
        }

        $choice = [string]$choiceResult.Value
        if ($choice.ToUpperInvariant() -eq 'S')
        {
            if ($selected.Count -eq 0)
            {
                Write-GameWipHost 'Select at least one editor or IDE.' -ForegroundColor Yellow
                continue
            }
            $orderedSelection = @(
                $EditorConfig.Options |
                    Where-Object { $selected.Contains($_.Id) } |
                    ForEach-Object { $_.Id }
            )
            Save-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -Editors $orderedSelection
            Write-Host "Saved editor selection: $($orderedSelection -join ', ')"
            return $true
        }

        $option = $EditorConfig.Options | Where-Object { $_.Key -eq $choice } | Select-Object -First 1
        if (-not $option)
        {
            Write-GameWipHost 'Press a listed number, S, or Esc.' -ForegroundColor Yellow
            continue
        }
        if (-not $selected.Add($option.Id))
        {
            $selected.Remove($option.Id) | Out-Null
        }
    }
}

function Install-GameWipEditorSelection
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][hashtable]$EditorConfig,
        [Parameter(Mandatory = $true)][string[]]$SelectedEditors,
        [switch]$Update
    )

    foreach ($id in $SelectedEditors)
    {
        $option = $EditorConfig.Options | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        switch ($option.Handler)
        {
            'vscode'
            {
                if ($Update -and (Test-GameWipWingetPackage -Id $option.Package))
                {
                    Invoke-GameWipWingetPackageUpdate -Id $option.Package
                }
                elseif (-not (Test-GameWipSetupCommand -Name $option.Command))
                {
                    Install-GameWipWingetPackage -Id $option.Package
                }
                Install-GameWipEditorIntegration -RepositoryRoot $RepositoryRoot -Extensions $option.Extensions
            }
            'visual-studio'
            {
                Install-GameWipVisualStudio -PackageId $option.Package -VsConfigPath (Join-Path $RepositoryRoot '.vsconfig') -Update:$Update
            }
            default
            {
                throw "Editor '$($option.Id)' uses unsupported setup handler '$($option.Handler)'."
            }
        }
    }
}

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
                if (-not (Test-GameWipSetupCommand -Name $option.Command))
                {
                    $failures.Add('Selected editor Visual Studio Code is missing.')
                    continue
                }
                $extensionRoot = Join-Path $env:USERPROFILE '.vscode\extensions'
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
                $workflowPackage = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'scripts\setup\editor\gamewip-workflows\package.json') -Raw | ConvertFrom-Json
                $expectedWorkflowExtension = "$($workflowPackage.publisher).$($workflowPackage.name)@$($workflowPackage.version)"
                $extensionQuery = Invoke-GameWipProcess -FilePath 'code' -Arguments @('--list-extensions', '--show-versions') -OutputMode LogOnly -TimeoutSeconds 30
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
                $keybindingsPath = Join-Path $env:APPDATA 'Code\User\keybindings.json'
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

function Install-GameWipVsCodeKeybinding
{
    param([Parameter(Mandatory = $true)][string]$ExtensionSource)

    if (-not $env:APPDATA)
    {
        throw 'APPDATA is unavailable; cannot locate the Visual Studio Code user keybindings file.'
    }
    $packagePath = Join-Path $ExtensionSource 'package.json'
    $package = Get-Content -LiteralPath $packagePath -Raw | ConvertFrom-Json
    $rules = @($package.contributes.keybindings)
    if ($rules.Count -eq 0)
    {
        throw "The GameWIP workflow extension declares no keybindings in $packagePath."
    }

    $userDirectory = Join-Path $env:APPDATA 'Code\User'
    $keybindingsPath = Join-Path $userDirectory 'keybindings.json'
    $backupPath = "$keybindingsPath.gamewip-backup"
    New-Item -ItemType Directory -Path $userDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $keybindingsPath)
    {
        if (-not (Test-Path -LiteralPath $backupPath))
        {
            Copy-Item -LiteralPath $keybindingsPath -Destination $backupPath
            Write-Host "  Preserved original VS Code keybindings: $backupPath"
        }
        $content = Get-Content -LiteralPath $keybindingsPath -Raw
    }
    else
    {
        $content = "[`r`n]"
        Write-Host "  Creating VS Code keybindings file: $keybindingsPath"
    }

    $managedPattern = '(?ms)\s*,?\s*// GameWIP managed keybindings begin.*?// GameWIP managed keybindings end\s*'
    $content = [regex]::Replace($content, $managedPattern, '')
    $closingBracket = $content.LastIndexOf(']')
    if ($closingBracket -lt 0)
    {
        throw "VS Code keybindings are not a JSON array: $keybindingsPath"
    }

    $prefix = $content.Substring(0, $closingBracket).TrimEnd()
    $suffix = $content.Substring($closingBracket)
    $openingBracket = $prefix.IndexOf('[')
    if ($openingBracket -lt 0)
    {
        throw "VS Code keybindings are not a JSON array: $keybindingsPath"
    }
    $existingBody = $prefix.Substring($openingBracket + 1)
    $significantBody = [regex]::Replace($existingBody, '(?s)/\*.*?\*/', '')
    $significantBody = [regex]::Replace($significantBody, '(?m)//.*$', '').Trim()
    $separator = if (-not $significantBody -or $significantBody.EndsWith(','))
    {
        ''
    }
    else
    {
        ','
    }

    $serializedRules = for ($index = 0; $index -lt $rules.Count; ++$index)
    {
        $serializedRule = [ordered]@{
            key = $rules[$index].key
            command = $rules[$index].command
        }
        if ($rules[$index].PSObject.Properties['args'])
        {
            $serializedRule.args = $rules[$index].args
        }
        $serializedRule.when = $rules[$index].when
        $rule = $serializedRule | ConvertTo-Json -Compress
        $comma = if ($index -lt $rules.Count - 1)
        {
            ','
        }
        else
        {
            ''
        }
        "  $rule$comma"
    }
    $managedBlock = @(
        '// GameWIP managed keybindings begin - rerun setup to update'
        $serializedRules
        '// GameWIP managed keybindings end'
    ) -join "`r`n"
    $updatedContent = "$prefix$separator`r`n$managedBlock`r`n$suffix"
    [IO.File]::WriteAllText($keybindingsPath, $updatedContent, [Text.UTF8Encoding]::new($false))

    $verification = Get-Content -LiteralPath $keybindingsPath -Raw
    foreach ($rule in $rules)
    {
        if (-not $verification.Contains($rule.key) -or
            -not $verification.Contains($rule.command) -or
            ($rule.PSObject.Properties['args'] -and -not $verification.Contains($rule.args)))
        {
            throw "Managed VS Code keybinding verification failed for $($rule.key)."
        }
    }
    Write-Host "  Managed $($rules.Count) repository-scoped VS Code keybindings: $keybindingsPath"
}

function Invoke-GameWipVsCodeExtensionPackaging
{
    param(
        [Parameter(Mandatory = $true)][string]$ExtensionSource,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $package = Get-Content -LiteralPath (Join-Path $ExtensionSource 'package.json') -Raw | ConvertFrom-Json
    $stagingRoot = Join-Path $Script:OperationContext.Temp "gamewip-vsix-$([Guid]::NewGuid().ToString('N'))"
    $extensionRoot = Join-Path $stagingRoot 'extension'
    try
    {
        New-Item -ItemType Directory -Path $extensionRoot -Force | Out-Null
        Copy-Item -Path (Join-Path $ExtensionSource '*') -Destination $extensionRoot -Recurse -Force

        $identity = "$($package.publisher).$($package.name)"
        $manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="$identity" Version="$($package.version)" Publisher="$($package.publisher)" />
    <DisplayName>$($package.displayName)</DisplayName>
    <Description xml:space="preserve">$($package.description)</Description>
    <Tags>vscode</Tags>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($package.engines.vscode)" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property Id="Microsoft.VisualStudio.Services.Links.Source" Value="https://github.com/Andrei11033/GameWIP" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
"@
        $contentTypes = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>
"@
        [IO.File]::WriteAllText((Join-Path $stagingRoot 'extension.vsixmanifest'), $manifest, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $stagingRoot '[Content_Types].xml'), $contentTypes, [Text.UTF8Encoding]::new($false))

        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
        if (Test-Path -LiteralPath $Destination)
        {
            Remove-Item -LiteralPath $Destination -Force
        }
        $archive = [IO.Compression.ZipFile]::Open($Destination, [IO.Compression.ZipArchiveMode]::Create)
        try
        {
            foreach ($file in Get-ChildItem -LiteralPath $stagingRoot -File -Recurse)
            {
                $entryName = $file.FullName.Substring($stagingRoot.Length + 1).Replace('\', '/')
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive,
                    $file.FullName,
                    $entryName,
                    [IO.Compression.CompressionLevel]::Optimal
                ) | Out-Null
            }
        }
        finally
        {
            $archive.Dispose()
        }
    }
    finally
    {
        if (Test-Path -LiteralPath $stagingRoot)
        {
            Invoke-GameWipOwnedTreeRemoval -Path $stagingRoot -OwnedRoot $Script:OperationContext.Temp
        }
    }
}

function Install-GameWipEditorIntegration
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Extensions
    )

    if (-not (Test-GameWipSetupCommand -Name 'code'))
    {
        throw 'Visual Studio Code was not found after installation.'
    }

    foreach ($extension in $Extensions)
    {
        Write-Host "  Checking Visual Studio Code extension: $extension"
        $extensionQuery = Invoke-GameWipProcess -FilePath 'code' -Arguments @('--list-extensions') -OutputMode LogOnly -TimeoutSeconds 30
        if ($extensionQuery.ExitCode -ne 0)
        {
            throw "Visual Studio Code extension discovery failed with exit code $($extensionQuery.ExitCode)."
        }
        $installedBefore = @($extensionQuery.Stdout) -contains $extension
        Invoke-GameWipSetupNative -FilePath 'code' -ArgumentList @('--install-extension', $extension, '--force') | Out-Null
        if (-not $installedBefore)
        {
            Add-GameWipOwnedVsCodeExtension -Id $extension
        }
    }

    $source = Join-Path $RepositoryRoot 'scripts\setup\editor\gamewip-workflows'
    $package = Get-Content -LiteralPath (Join-Path $source 'package.json') -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        throw 'Editor integration requires an initialized GameWIP operation-temp directory.'
    }
    $vsixPath = Join-Path $Script:OperationContext.Temp 'gamewip-workflows.vsix'
    Invoke-GameWipVsCodeExtensionPackaging -ExtensionSource $source -Destination $vsixPath
    $extensionId = "$($package.publisher).$($package.name)"
    Write-Host "  Installing Visual Studio Code extension package: $vsixPath"
    Invoke-GameWipSetupNative -FilePath 'code' -ArgumentList @('--install-extension', $vsixPath, '--force') | Out-Null
    $extensionQuery = Invoke-GameWipProcess -FilePath 'code' -Arguments @('--list-extensions', '--show-versions') -OutputMode LogOnly -TimeoutSeconds 30
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
