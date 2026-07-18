Set-StrictMode -Version Latest

function Get-GameWipEditorPreferencePath
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    return (Join-Path $RepositoryRoot '.gamewip-setup.json')
}

function Get-GameWipSelectedEditors
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

    $preference = Get-Content -LiteralPath $preferencePath -Raw | ConvertFrom-Json
    $selected = @($preference.editors)
    $known = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@($EditorConfig.Options | ForEach-Object { $_.Id })
    )
    foreach ($id in $selected)
    {
        if (-not $known.Contains($id))
        {
            throw "Unknown editor '$id' in $preferencePath. Rerun the editor setup action to update the selection."
        }
    }
    return $selected
}

function Set-GameWipSelectedEditors
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Editors
    )

    $preferencePath = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    [ordered]@{
        schemaVersion = 1
        editors = @($Editors)
    } | ConvertTo-Json | Set-Content -LiteralPath $preferencePath -Encoding UTF8
}

function Select-GameWipEditors
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][hashtable]$EditorConfig
    )

    $selected = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@(Get-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    )
    while ($true)
    {
        Write-Host ''
        Write-Host 'Select editors and IDEs'
        Write-Host '======================='
        Write-Host 'Press a number to toggle an editor:'
        foreach ($option in $EditorConfig.Options)
        {
            $mark = if ($selected.Contains($option.Id)) { 'x' } else { ' ' }
            $recommended = if ($option.Recommended) { ' (recommended)' } else { '' }
            Write-Host "$($option.Key). [$mark] $($option.Name)$recommended"
        }
        Write-Host 'S. Save selection'
        Write-Host 'Esc. Cancel'
        Write-Host 'Choose: ' -NoNewline

        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape -or [int]$key.KeyChar -eq 27)
        {
            Write-Host 'Esc'
            return $false
        }

        $choice = $key.KeyChar.ToString()
        Write-Host $choice
        if ($choice.ToUpperInvariant() -eq 'S')
        {
            if ($selected.Count -eq 0)
            {
                Write-Host 'Select at least one editor or IDE.' -ForegroundColor Yellow
                continue
            }
            $orderedSelection = @(
                $EditorConfig.Options |
                    Where-Object { $selected.Contains($_.Id) } |
                    ForEach-Object { $_.Id }
            )
            Set-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -Editors $orderedSelection
            Write-Host "Saved editor selection: $($orderedSelection -join ', ')"
            return $true
        }

        $option = $EditorConfig.Options | Where-Object { $_.Key -eq $choice } | Select-Object -First 1
        if (-not $option)
        {
            Write-Host 'Press a listed number, S, or Esc.' -ForegroundColor Yellow
            continue
        }
        if (-not $selected.Add($option.Id))
        {
            $selected.Remove($option.Id) | Out-Null
        }
    }
}

function Install-GameWipSelectedEditors
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
            'vscode' {
                if ($Update -and (Test-WingetPackage -Id $option.Package))
                {
                    Update-WingetPackage -Id $option.Package
                }
                elseif (-not (Test-SetupCommand -Name $option.Command))
                {
                    Install-WingetPackage -Id $option.Package
                }
                Install-GameWipEditorIntegration -RepositoryRoot $RepositoryRoot -Extensions $option.Extensions
            }
            'visual-studio' {
                Install-GameWipVisualStudio -PackageId $option.Package -VsConfigPath (Join-Path $RepositoryRoot '.vsconfig') -Update:$Update
            }
            default { throw "Editor '$($option.Id)' uses unsupported setup handler '$($option.Handler)'." }
        }
    }
}

function Get-GameWipEditorFailures
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
            'vscode' {
                if (-not (Test-SetupCommand -Name $option.Command))
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
                $workflowExtension = Join-Path $env:USERPROFILE '.vscode\extensions\gamewip.gamewip-workflows\package.json'
                if (-not (Test-Path -LiteralPath $workflowExtension))
                {
                    $failures.Add('The GameWIP VS Code workflow-keybinding extension is not installed.')
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
            'visual-studio' {
                if (-not (Get-VisualStudioInstance))
                {
                    $failures.Add('Selected IDE Visual Studio with the C++ desktop workload is missing.')
                }
            }
            default { $failures.Add("Selected editor '$id' has an unsupported setup handler.") }
        }
    }
    return $failures
}

function Install-GameWipVsCodeKeybindings
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
    $separator = if (-not $significantBody -or $significantBody.EndsWith(',')) { '' } else { ',' }

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
        $comma = if ($index -lt $rules.Count - 1) { ',' } else { '' }
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

function Install-GameWipEditorIntegration
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Extensions
    )

    if (-not (Test-SetupCommand -Name 'code'))
    {
        throw 'Visual Studio Code was not found after installation.'
    }

    foreach ($extension in $Extensions)
    {
        Write-Host "  Checking Visual Studio Code extension: $extension"
        $installedBefore = @(& code --list-extensions) -contains $extension
        Invoke-SetupNative -FilePath 'code' -ArgumentList @('--install-extension', $extension, '--force') | Out-Null
        if (-not $installedBefore) { Add-GameWipOwnedVsCodeExtension -Id $extension }
    }

    $source = Join-Path $RepositoryRoot 'scripts\setup\editor\gamewip-workflows'
    $target = Join-Path $env:USERPROFILE '.vscode\extensions\gamewip.gamewip-workflows'
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $target -Recurse -Force

    $sourcePackage = Get-Content -LiteralPath (Join-Path $source 'package.json') -Raw | ConvertFrom-Json
    $installedPackage = Get-Content -LiteralPath (Join-Path $target 'package.json') -Raw | ConvertFrom-Json
    if ($sourcePackage.version -ne $installedPackage.version)
    {
        throw 'The GameWIP VS Code keybinding extension version did not verify after installation.'
    }
    Install-GameWipVsCodeKeybindings -ExtensionSource $source
    Write-Host "  Ready: GameWIP workflow keybindings $($installedPackage.version)"
}
