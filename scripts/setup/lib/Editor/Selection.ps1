# Editor selection and readiness checks.

# ------------------------------------------------------------
# Editor selection and advisory state
# ------------------------------------------------------------

function Get-GameWipAvailableEditorOptions
{
    param([Parameter(Mandatory = $true)][hashtable]$EditorConfig)
    $hostPlatform = if (Test-GameWipWindowsHost)
    {
        'windows'
    }
    else
    {
        'linux'
    }

    return @($EditorConfig.Options | Where-Object {
            (-not $_.ContainsKey('Platforms')) -or @($_.Platforms) -contains $hostPlatform
        })
}

# ------------------------------------------------------------
# Selection persistence and interactive choice
# ------------------------------------------------------------

function Get-GameWipEditorPreferencePath
{
    return Join-Path (Get-GameWipStorageAreaPath -Area state) 'editor-selection.json'
}

function Get-GameWipEditorSelection
{
    param([Parameter(Mandatory = $true)][hashtable]$EditorConfig)

    $preferencePath = Get-GameWipEditorPreferencePath

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

        $availableOptions = @(Get-GameWipAvailableEditorOptions -EditorConfig $EditorConfig)
        $known = [System.Collections.Generic.HashSet[string]]::new(
            [string[]]@($availableOptions | ForEach-Object { $_.Id })
        )

        foreach ($id in $selected)
        {
            if (-not $known.Contains($id))
            {
                $configured = @($EditorConfig.Options | Where-Object { $_.Id -eq $id })

                if ($configured.Count -eq 0)
                {
                    throw "Unknown editor '$id'."
                }

                Write-Verbose "Skipping editor '$id' because it is unavailable on this host."
            }
        }

        return @($selected | Where-Object { $known.Contains([string]$_) })
    }

    catch
    {
        Write-Warning "Ignoring corrupt advisory editor state '$preferencePath': $($_.Exception.Message)"
        return @($EditorConfig.Default)
    }
}

function Save-GameWipEditorSelection
{
    param([Parameter(Mandatory = $true)][string[]]$Editors)

    $preferencePath = Get-GameWipEditorPreferencePath
    $preference = [ordered]@{
        schemaVersion = 1
        editors = @($Editors)
    }
    Write-GameWipJsonAtomic -Path $preferencePath -Value $preference -Depth 4
}

function Select-GameWipEditor
{
    param([Parameter(Mandatory = $true)][hashtable]$EditorConfig)

    $selected = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@(Get-GameWipEditorSelection -EditorConfig $EditorConfig)
    )

    while ($true)
    {
        Write-GameWipSection 'Select editors and IDEs'
        Write-Host 'Press a number to toggle an editor:'

        $availableOptions = @(Get-GameWipAvailableEditorOptions -EditorConfig $EditorConfig)

        foreach ($option in $availableOptions)
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
        $allowedKeys = @($availableOptions | ForEach-Object { [string]$_.Key }) + @('S', 's')
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
                $availableOptions |
                    Where-Object { $selected.Contains($_.Id) } |
                    ForEach-Object { $_.Id }
            )
            Save-GameWipEditorSelection -Editors $orderedSelection
            Write-Host "Saved editor selection: $($orderedSelection -join ', ')"
            return $true
        }

        $option = $availableOptions | Where-Object { $_.Key -eq $choice } | Select-Object -First 1

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
                $installEditor = $true

                if (-not (Test-GameWipSetupCommand -Name $option.Command))
                {
                    Add-GameWipOperationPreserved -Message "Left $($option.Name) unchanged because it is not installed."
                    Write-GameWipSemanticText -Object "$($option.Name) is not installed; editor integration was skipped." -Semantic Warning
                    $installEditor = $false
                }

                if ($installEditor)
                {
                    Install-GameWipEditorIntegration `
                        -RepositoryRoot $RepositoryRoot `
                        -Extensions $option.Extensions `
                        -Update:$Update
                }
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
