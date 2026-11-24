# VS Code managed keybindings.

# ------------------------------------------------------------
# Validate and update keybindings
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Install-GameWipVsCodeKeybinding
{
    param([Parameter(Mandatory = $true)][string]$ExtensionSource)

    # Validate the declared rules before editing the user's file.
    $packagePath = Join-Path $ExtensionSource 'package.json'
    $package = Get-Content -LiteralPath $packagePath -Raw | ConvertFrom-Json
    $rules = @($package.contributes.keybindings)

    if ($rules.Count -eq 0)
    {
        throw "The GameWIP workflow extension declares no keybindings in $packagePath."
    }

    # Keep the original under build/ so uninstall removes helper-owned data.
    $userDirectory = Get-GameWipVsCodeUserDirectory
    $keybindingsPath = Join-Path $userDirectory 'keybindings.json'
    $legacyBackupPath = "$keybindingsPath.gamewip-backup"
    $backupPath = Join-Path (Get-GameWipStorageAreaPath -Area state) 'vscode-keybindings.json'
    New-Item -ItemType Directory -Path $userDirectory -Force | Out-Null

    if (Test-Path -LiteralPath $keybindingsPath)
    {
        $backupAttributes = Get-GameWipFileSystemAttributes -Path $backupPath

        if ($null -ne $backupAttributes -and
            (($backupAttributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            ($backupAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0))
        {
            throw "Refusing to write the VS Code keybindings backup through a linked or invalid path '$backupPath'."
        }

        if (-not (Test-Path -LiteralPath $backupPath))
        {
            New-Item -ItemType Directory -Path (Split-Path -Parent $backupPath) -Force | Out-Null
            $legacyBackupAttributes = Get-GameWipFileSystemAttributes -Path $legacyBackupPath

            if ($null -ne $legacyBackupAttributes -and
                ($legacyBackupAttributes -band [IO.FileAttributes]::Directory) -eq 0 -and
                ($legacyBackupAttributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)
            {
                Copy-Item -LiteralPath $legacyBackupPath -Destination $backupPath
                Remove-Item -LiteralPath $legacyBackupPath -Force
            }
            else
            {
                Copy-Item -LiteralPath $keybindingsPath -Destination $backupPath
            }

            Write-Host "  Preserved original VS Code keybindings: $backupPath"
        }

        $content = Get-Content -LiteralPath $keybindingsPath -Raw
    }
    else
    {
        $content = "[`r`n]"
        Write-Host "  Creating VS Code keybindings file: $keybindingsPath"
    }

    # Replace the managed block and preserve the user's JSON-with-comments.
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
    Write-GameWipTextAtomic -Path $keybindingsPath -Content $updatedContent

    # Verify each rule after the atomic write.
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
