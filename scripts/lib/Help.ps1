# GameWIP Help helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Write-GameWipSection
{
    param([Parameter(Mandatory = $true)][string]$Title)

    Write-Host ''
    Write-Host $Title
    Write-Host ('=' * $Title.Length)
}

function Read-GameWipIndexedChoice
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices
    )
    if ($Choices.Count -eq 0)
    {
        return $null
    }
    Write-Host ''
    Write-Host $Prompt
    for ($index = 0; $index -lt $Choices.Count; ++$index)
    {
        Write-Host ("  [{0}] {1}" -f ($index + 1), $Choices[$index])
    }
    while ($true)
    {
        $answer = Read-Host 'Selection [Q = cancel]'
        if ($answer -eq 'q' -or $answer -eq 'Q' -or [string]::IsNullOrWhiteSpace($answer))
        {
            return $null
        }
        $number = 0
        if ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and $number -le $Choices.Count)
        {
            return $Choices[$number - 1]
        }
        Write-Host 'Enter one of the listed numbers or Q.' -ForegroundColor Yellow
    }
}

function Show-GameWipProjectCatalog
{
    Write-GameWipSection 'Project helper actions'
    Write-Host '  menu, doctor, git, workflow, unicode, format, quality, tools, links'
    Write-Host '  configure, build, test, wizard, module, stress, run, bundle'
    Write-Host '  docs, analysis, analyze, coverage, asan, benchmark, list, help'

    Write-GameWipSection 'Configure presets'
    Get-GameWipVisiblePresetName -Kind 'configure' | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Build presets'
    Get-GameWipVisiblePresetName -Kind 'build' | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Test presets'
    Get-GameWipVisiblePresetName -Kind 'test' | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Validation modules'
    @($CommandConfig.Modules) | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Unicode data maintenance'
    Write-Host ("  version - Unicode {0}" -f $CommandConfig.Unicode.Version)
    Write-Host '  actions - status, verify, regenerate'

    Write-GameWipSection 'Project commands'
    foreach ($command in $CommandConfig.ProjectCommands)
    {
        Write-Host ("  {0} - {1}" -f $command.Id, $command.Name)
    }

    Write-GameWipSection 'Benchmark profiles'
    foreach ($benchmarkProfile in $CommandConfig.BenchmarkProfiles)
    {
        Write-Host ("  {0} - {1} ({2} repetitions, min {3})" -f $benchmarkProfile.Id, $benchmarkProfile.Name, $benchmarkProfile.Repetitions, $benchmarkProfile.MinTime)
    }

    Write-GameWipSection 'Bundles'
    foreach ($bundleInfo in $CommandConfig.Bundles)
    {
        Write-Host ("  {0} - {1}" -f $bundleInfo.Id, $bundleInfo.Name)
    }

    Show-GameWipWorkflowCatalog
}

function Read-GameWipMenuChoice
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices,
        [string]$Default
    )

    $keys = @('1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'a', 'b', 'c', 'd', 'e', 'f')
    if ($Choices.Count -gt $keys.Count)
    {
        throw "Too many choices for single-key selection: $($Choices.Count)."
    }

    while ($true)
    {
        Write-Host ''
        Write-Host $Prompt
        for ($index = 0; $index -lt $Choices.Count; ++$index)
        {
            Write-Host ("  [{0}] {1}" -f $keys[$index], $Choices[$index])
        }
        if (-not [string]::IsNullOrWhiteSpace($Default))
        {
            Write-Host "  [Enter] $Default"
        }
        Write-Host 'Choose one key, or ESC/q to cancel: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::ESCape -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q')
        {
            Write-Host 'cancel'
            return $null
        }
        if ($key.Key -eq [ConsoleKey]::Enter -and -not [string]::IsNullOrWhiteSpace($Default))
        {
            Write-Host 'Enter'
            return $Default
        }

        $selectionKey = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $selectionKey
        $index = [array]::IndexOf($keys, $selectionKey)
        if ($index -ge 0 -and $index -lt $Choices.Count)
        {
            return $Choices[$index]
        }
        Write-Host 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Read-GameWipTextValue
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [string]$Default = ''
    )

    if ([string]::IsNullOrWhiteSpace($Default))
    {
        $value = Read-Host $Prompt
    }
    else
    {
        $value = Read-Host "$Prompt [$Default]"
    }
    if ([string]::IsNullOrWhiteSpace($value))
    {
        return $Default
    }
    $value
}

function Read-GameWipIntegerValue
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][int]$Default
    )

    $values = @(1, 2, 4, 8, 16, 32, 64, 100)
    $keys = @('1', '2', '3', '4', '5', '6', '7', '8')
    while ($true)
    {
        Write-Host ''
        Write-Host $Prompt
        for ($index = 0; $index -lt $values.Count; ++$index)
        {
            Write-Host ("  [{0}] {1}" -f $keys[$index], $values[$index])
        }
        Write-Host "  [Enter] $Default"
        Write-Host '  [c] custom'
        Write-Host 'Choose one key: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            return $Default
        }
        $selectionKey = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $selectionKey
        if ($selectionKey -eq 'c')
        {
            $value = Read-GameWipTextValue -Prompt 'Custom positive integer' -Default ([string]$Default)
            $parsed = 0
            if ([int]::TryParse($value, [ref]$parsed) -and $parsed -gt 0)
            {
                return $parsed
            }
            Write-Host 'Enter a positive integer.' -ForegroundColor Yellow
            continue
        }

        $index = [array]::IndexOf($keys, $selectionKey)
        if ($index -ge 0)
        {
            return $values[$index]
        }
        Write-Host 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Read-GameWipYesNo
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][bool]$Default
    )

    $suffix = if ($Default)
    {
        '[Y/n]'
    }
    else
    {
        '[y/N]'
    }
    while ($true)
    {
        Write-Host "$Prompt $suffix " -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            return $Default
        }
        $value = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $value
        switch ($value)
        {
            'y'
            {
                return $true
            }
            'n'
            {
                return $false
            }
            default
            {
                Write-Host 'Enter y or n.' -ForegroundColor Yellow
            }
        }
    }
}

function Read-GameWipMultiChoice
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices
    )

    $keys = @('1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'a', 'b', 'c', 'd', 'e', 'f')
    if ($Choices.Count -gt $keys.Count)
    {
        throw "Too many choices for single-key selection: $($Choices.Count)."
    }

    $selected = New-Object System.Collections.Generic.HashSet[string]
    while ($true)
    {
        Write-Host ''
        Write-Host $Prompt
        for ($index = 0; $index -lt $Choices.Count; ++$index)
        {
            $marker = if ($selected.Contains($Choices[$index]))
            {
                'x'
            }
            else
            {
                ' '
            }
            Write-Host ("  [{0}] [{1}] {2}" -f $keys[$index], $marker, $Choices[$index])
        }
        Write-Host 'Toggle one key, Enter to accept, or ESC/q to cancel: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::ESCape -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q')
        {
            Write-Host 'cancel'
            return @()
        }
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            $result = New-Object System.Collections.Generic.List[string]
            foreach ($choice in $selected)
            {
                $result.Add($choice) | Out-Null
            }
            return $result.ToArray()
        }

        $selectionKey = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $selectionKey
        $index = [array]::IndexOf($keys, $selectionKey)
        if ($index -ge 0 -and $index -lt $Choices.Count)
        {
            $choice = $Choices[$index]
            if ($selected.Contains($choice))
            {
                [void]$selected.Remove($choice)
            }
            else
            {
                [void]$selected.Add($choice)
            }
            continue
        }

        Write-Host 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Show-GameWipActionFailure
{
    param([Parameter(Mandatory = $true)][System.Management.Automation.ErrorRecord]$ErrorRecord)

    $message = $ErrorRecord.Exception.Message
    Write-Host ''
    Write-Host 'Action failed' -ForegroundColor Red
    Write-Host $message -ForegroundColor Red

    $suggestions = New-Object System.Collections.Generic.List[string]
    if ($message -match "Unknown .+ Run 'gamewip list'")
    {
        $suggestions.Add('Run .\gamewip.bat list to see the valid presets, modules, commands, and bundles.') | Out-Null
    }
    if ($message -match "Missing executable '([^']+)'.+build preset '([^']+)'")
    {
        $suggestions.Add(("Build the missing executable with .\gamewip.bat build -Preset {0}." -f $Matches[2])) | Out-Null
        $suggestions.Add('For project commands and modules, add -BuildIfMissing when you want the helper to configure/build first.') | Out-Null
    }
    if ($message -match 'failed with exit code')
    {
        $suggestions.Add('Open the step log path printed above; it has the complete native command output.') | Out-Null
        $suggestions.Add('Rerun the smallest focused command: .\gamewip.bat wizard or .\gamewip.bat module -Module <name>.') | Out-Null
    }
    if ($message -match 'GitHub CLI|GitHub authentication|workflow scope|project scope')
    {
        $suggestions.Add("Check authentication with 'gh auth status', then refresh the scopes named above.") | Out-Null
    }
    if ($message -match 'workflow|Workflow')
    {
        $suggestions.Add("Preview a safe command with '.\gamewip.bat workflow -WorkflowAction run -Workflow <id> -Preview'.") | Out-Null
        $suggestions.Add("List supported workflows with '.\gamewip.bat workflow -WorkflowAction list'.") | Out-Null
    }
    if ($message -match 'stress runs failed')
    {
        $suggestions.Add('Inspect the stress-####.log and stress-####.err.log files under the printed run logs folder.') | Out-Null
    }
    if ($message -match 'Benchmark|benchmark')
    {
        $suggestions.Add('List registered scenarios with .\gamewip.bat benchmark -BenchmarkAction list -NoBuild.') | Out-Null
        $suggestions.Add('Use the quick profile and a filter for a small diagnostic run.') | Out-Null
    }
    if ($message -match 'Logger|logger')
    {
        $suggestions.Add('For rare Logger failures, rerun .\gamewip.bat stress -Module logger -Count 100 -Parallel 16 -BuildIfMissing to measure repeatability.') | Out-Null
    }
    if ($message -match 'Unicode|UCRT64 Python|Python override|GAMEWIP_PYTHON|clang-format|ClangFormatPath|GAMEWIP_CLANG_FORMAT')
    {
        $suggestions.Add('Inspect Unicode tooling with .\gamewip.bat unicode -UnicodeAction status.') | Out-Null
        $suggestions.Add('If UCRT64 Python or clang-format is missing, run .\setup.bat repair; GameWIP setup owns those dependencies.') | Out-Null
    }
    if ($suggestions.Count -eq 0)
    {
        $suggestions.Add('Rerun the same command after reviewing the printed native command and run-log folder.') | Out-Null
        $suggestions.Add('Use .\gamewip.bat list if the failure was caused by an unknown command, module, or preset.') | Out-Null
    }

    Write-Host ''
    Write-Host 'What to do next:' -ForegroundColor Cyan
    foreach ($suggestion in ($suggestions | Select-Object -Unique))
    {
        Write-Host "  - $suggestion"
    }
}

function Invoke-GameWipInteractivePostBuildFlow
{
    param([Parameter(Mandatory = $true)][string]$Name)

    if ((Get-GameWipVisiblePresetName -Kind 'test') -contains $Name)
    {
        if (Read-GameWipYesNo -Prompt "Run CTest preset '$Name' now?" -Default $true)
        {
            Invoke-GameWipTestPreset -Name $Name -UseWorkspaceTemp
            if ($Name -eq 'coverage' -and (Read-GameWipYesNo -Prompt "Generate the coverage report target now?" -Default $true))
            {
                Invoke-GameWipBuildTarget -Name 'coverage' -Target 'coverage'
            }
        }
        else
        {
            Write-GameWipNextStepHint "run tests with: .\gamewip.bat test -Preset $Name"
        }
        return
    }

    switch ($Name)
    {
        'benchmark'
        {
            if (Read-GameWipYesNo -Prompt 'Run the standard benchmark profile now?' -Default $true)
            {
                Invoke-GameWipBenchmark -Mode 'run' -ProfileId 'standard' -RepeatCount 0 -MinimumTime '' -RequestedOutput '' -Format 'json' -SkipBuild
            }
            else
            {
                Write-GameWipNextStepHint 'run it later with: .\gamewip.bat benchmark; use -BenchmarkAction dry-run for registration only.'
            }
        }
        'dev'
        {
            if (Read-GameWipYesNo -Prompt 'Print the development executable version now?' -Default $true)
            {
                Invoke-GameWipProjectCommand -Id 'dev-version' -ForceBuild
            }
            else
            {
                Write-GameWipNextStepHint 'print it later with: .\gamewip.bat run -ProjectCommand dev-version'
            }
        }
        'release'
        {
            if (Read-GameWipYesNo -Prompt 'Print the release executable version now?' -Default $true)
            {
                Invoke-GameWipProjectCommand -Id 'release-version' -ForceBuild
            }
            else
            {
                Write-GameWipNextStepHint 'print it later with: .\gamewip.bat run -ProjectCommand release-version'
            }
        }
        'docs'
        {
            Write-GameWipNextStepHint 'open build\docs\docs\doxygen\html\index.html to inspect the generated manual.'
        }
        'analyze'
        {
            Write-GameWipNextStepHint 'static analysis is complete when the build exits successfully.'
        }
        default
        {
            Write-GameWipNextStepHint 'use .\gamewip.bat list to see follow-up project commands and bundles.'
        }
    }
}

function Invoke-GameWipInteractiveConfigureFlow
{
    param([Parameter(Mandatory = $true)][string]$Name)

    Invoke-GameWipConfigurePreset -Name $Name
    if ((Get-GameWipVisiblePresetName -Kind 'build') -contains $Name)
    {
        if (Read-GameWipYesNo -Prompt "Build preset '$Name' now?" -Default $true)
        {
            Invoke-GameWipBuildPreset -Name $Name
            Invoke-GameWipInteractivePostBuildFlow -Name $Name
        }
        else
        {
            Write-GameWipNextStepHint "build it with: .\gamewip.bat build -Preset $Name"
        }
    }
}

function Invoke-GameWipInteractiveBuildFlow
{
    param([Parameter(Mandatory = $true)][string]$Name)

    Invoke-GameWipBuildPreset -Name $Name
    Invoke-GameWipInteractivePostBuildFlow -Name $Name
}

function Show-GameWipQualityMenu
{
    while ($true)
    {
        Write-Host ''
        Write-Host 'Quality and Maintenance'
        Write-Host '======================='
        Write-Host '1. Check C/C++ formatting'
        Write-Host '2. Apply C/C++ formatting'
        Write-Host '3. Run static analysis'
        Write-Host '4. Run AddressSanitizer validation'
        Write-Host '5. Run coverage validation'
        Write-Host '6. Build documentation'
        Write-Host '7. Run performance benchmarks'
        Write-Host '8. Run full local release-readiness bundle'
        Write-Host 'ESC. Back'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::ESCape)
        {
            Write-Host 'ESC'; return
        }
        Write-Host $key.KeyChar

        switch ($key.KeyChar)
        {
            '1'
            {
                Invoke-GameWipFormat -Mode 'check'
            }
            '2'
            {
                Invoke-GameWipFormat -Mode 'apply'
            }
            '3'
            {
                Invoke-GameWipConfigurePreset -Name 'analyze'
                Invoke-GameWipBuildPreset -Name 'analyze'
            }
            '4'
            {
                Invoke-GameWipConfigurePreset -Name 'asan'
                Invoke-GameWipBuildPreset -Name 'asan'
                Invoke-GameWipTestPreset -Name 'asan' -UseWorkspaceTemp
            }
            '5'
            {
                Invoke-GameWipConfigurePreset -Name 'coverage'
                Invoke-GameWipBuildPreset -Name 'coverage'
                Invoke-GameWipTestPreset -Name 'coverage' -UseWorkspaceTemp
                Invoke-GameWipBuildTarget -Name 'coverage' -Target 'coverage'
            }
            '6'
            {
                Invoke-GameWipConfigurePreset -Name 'docs'
                Invoke-GameWipBuildPreset -Name 'docs'
            }
            '7'
            {
                Invoke-GameWipBenchmark -Mode 'run' -ProfileId 'standard' -RepeatCount 0 -MinimumTime '' -RequestedOutput '' -Format 'json'
            }
            '8'
            {
                Invoke-GameWipBundle -Id 'local-release-check'
            }
            default
            {
                Write-Host 'Press 1-8 or ESC.' -ForegroundColor Yellow
            }
        }
    }
}

function Show-GameWipMenu
{
    while ($true)
    {
        Write-Host ''
        Write-Host 'GameWIP Project Tool'
        Write-Host '===================='
        Write-Host '1. Configure preset'
        Write-Host '2. Build preset'
        Write-Host '3. Run CTest preset'
        Write-Host '4. Build/run a validation command'
        Write-Host '5. Run one validation module'
        Write-Host '6. Stress validation module'
        Write-Host '7. Run known project command'
        Write-Host '8. Run command bundle'
        Write-Host '9. List commands and presets'
        Write-Host '0. Check project readiness'
        Write-Host 'G. Git branches and workspace cleanup'
        Write-Host 'W. Guarded GitHub workflows'
        Write-Host 'Q. Quality and maintenance'
        Write-Host 'U. Unicode data maintenance'
        Write-Host 'ESC. Exit'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::ESCape -or [int]$key.KeyChar -eq 27)
        {
            Write-Host 'ESC'
            return
        }
        Write-Host $key.KeyChar

        try
        {
            switch ($key.KeyChar)
            {
                '1'
                {
                    $choice = Read-GameWipMenuChoice -Prompt 'Configure preset' -Choices (Get-GameWipVisiblePresetName -Kind 'configure') -Default $CommandConfig.DefaultConfigurePreset
                    if ($null -ne $choice)
                    {
                        Invoke-GameWipInteractiveConfigureFlow -Name $choice
                    }
                }
                '2'
                {
                    $choice = Read-GameWipMenuChoice -Prompt 'Build preset' -Choices (Get-GameWipVisiblePresetName -Kind 'build') -Default $CommandConfig.DefaultBuildPreset
                    if ($null -ne $choice)
                    {
                        Invoke-GameWipInteractiveBuildFlow -Name $choice
                    }
                }
                '3'
                {
                    $choice = Read-GameWipMenuChoice -Prompt 'CTest preset' -Choices (Get-GameWipVisiblePresetName -Kind 'test') -Default $CommandConfig.DefaultTestPreset
                    if ($null -ne $choice)
                    {
                        Invoke-GameWipTestPreset -Name $choice -UseWorkspaceTemp
                        Write-GameWipNextStepHint 'use option 4 to build a focused GameWIPTests.exe command, or option 6 to stress a module.'
                    }
                }
                '4'
                {
                    Invoke-GameWipValidationCommandWizard
                }
                '5'
                {
                    $choices = @('all') + @($CommandConfig.Modules)
                    $choice = Read-GameWipMenuChoice -Prompt 'Validation module' -Choices $choices -Default $CommandConfig.DefaultModule
                    if ($null -ne $choice)
                    {
                        Invoke-GameWipValidationModule -Name $choice -ForceBuild
                        Write-GameWipNextStepHint "stress this module with: .\gamewip.bat stress -Module $choice -Count 100 -Parallel 16"
                    }
                }
                '6'
                {
                    $choices = @('all') + @($CommandConfig.Modules)
                    $choice = Read-GameWipMenuChoice -Prompt 'Stress validation module' -Choices $choices -Default $CommandConfig.DefaultModule
                    if ($null -ne $choice)
                    {
                        $runCount = Read-GameWipIntegerValue -Prompt 'Run count' -Default ([int]$CommandConfig.DefaultStressCount)
                        $parallelCount = Read-GameWipIntegerValue -Prompt 'Parallel workers' -Default ([int]$CommandConfig.DefaultStressParallel)
                        Invoke-GameWipStressModule -Name $choice -RunCount $runCount -MaxParallel $parallelCount -ForceBuild
                    }
                }
                '7'
                {
                    $choices = @($CommandConfig.ProjectCommands | ForEach-Object { $_.Id })
                    $choice = Read-GameWipMenuChoice -Prompt 'Project command' -Choices $choices -Default 'benchmark-dry-run'
                    if ($null -ne $choice)
                    {
                        Invoke-GameWipProjectCommand -Id $choice -ForceBuild
                    }
                }
                '8'
                {
                    $choices = @($CommandConfig.Bundles | ForEach-Object { $_.Id })
                    $choice = Read-GameWipMenuChoice -Prompt 'Command bundle' -Choices $choices -Default 'quick'
                    if ($null -ne $choice)
                    {
                        Invoke-GameWipBundle -Id $choice
                    }
                }
                '9'
                {
                    Show-GameWipProjectCatalog
                }
                '0'
                {
                    Test-GameWipProjectReadiness | Out-Null
                }
                { $_ -eq 'g' -or $_ -eq 'G' }
                {
                    Show-GameWipGitMenu
                }
                { $_ -eq 'w' -or $_ -eq 'W' }
                {
                    Show-GameWipWorkflowMenu
                }
                { $_ -eq 'q' -or $_ -eq 'Q' }
                {
                    Show-GameWipQualityMenu
                }
                { $_ -eq 'u' -or $_ -eq 'U' }
                {
                    Show-GameWipUnicodeMenu
                }
                default
                {
                    Write-Host 'Press one of the listed number keys, or ESC to exit.' -ForegroundColor Yellow
                }
            }
        }
        catch
        {
            $Script:RunFailed = $true
            Show-GameWipActionFailure -ErrorRecord $_
        }
        finally
        {
            Save-GameWipRunSummary
        }
    }
}

function Show-GameWipHelp
{
    Write-Host 'Usage:'
    Write-Host '  .\gamewip.bat [action] [options]'
    Write-Host '  .\gamewip.bat help | --help | -h | -?'
    Write-Host ''
    Write-Host 'Interactive and discovery actions:'
    Write-Host '  menu                         Open the interactive project menu (default).'
    Write-Host '  doctor                       Check repository metadata and required tools.'
    Write-Host '  list                         List presets, modules, commands, bundles, and workflows.'
    Write-Host '  help                         Print this reference.'
    Write-Host ''
    Write-Host 'Workspace and maintenance actions:'
    Write-Host '  git [-GitAction <menu|status|fetch|switch|update|cleanup|create|push|log>] [-GitBranch <name>]'
    Write-Host '  workflow [-WorkflowAction <menu|list|status|run>] [-Workflow <id>] [-WorkflowKind <all|issue|pull_request>]'
    Write-Host '           [-WorkflowNumber <number>] [-ReleaseCommit <sha>] [-Preview]'
    Write-Host '  unicode [-UnicodeAction <menu|status|verify|regenerate>] [-RefreshUnicodeData]'
    Write-Host '          [-UnicodeDataRoot <path>] [-PythonPath <path>] [-ClangFormatPath <path>]'
    Write-Host '  format [-FormatAction <check|apply>] [-ClangFormatPath <path>]'
    Write-Host '  quality [-QualityAction <check|fix>]'
    Write-Host '  tools [-ToolsAction <list|status|check-updates|update>] [-Tool <id|all>] [-Preview]'
    Write-Host '  links [-PythonPath <path>]    Validate maintained local Markdown links.'
    Write-Host ''
    Write-Host 'Build and validation actions:'
    Write-Host '  configure [-Preset <name>]    Configure a CMake preset (default: test).'
    Write-Host '  build [-Preset <name>]        Configure if needed, then build (default: test).'
    Write-Host '  test [-Preset <name>]         Configure/build if needed, then run CTest (default: test).'
    Write-Host '  wizard                        Build an interactive GameWIPTests.exe command.'
    Write-Host '  module [-Module <name>] [-ExtraArgs <args>] [-BuildIfMissing]'
    Write-Host '  stress [-Module <name>] [-Count <1..100000>] [-Parallel <1..256>] [-ExtraArgs <args>] [-BuildIfMissing]'
    Write-Host '  run [-ProjectCommand <id>] [-ExtraArgs <args>] [-BuildIfMissing]'
    Write-Host '  bundle [-Bundle <id>]'
    Write-Host ''
    Write-Host 'Quality actions:'
    Write-Host '  docs | analysis | analyze | coverage | asan'
    Write-Host '  benchmark [-BenchmarkAction <run|dry-run|list|compare>] [-BenchmarkProfile <quick|standard|stable>]'
    Write-Host '            [-Filter <regex>] [-Repetitions <count>] [-MinTime <time>] [-AggregatesOnly]'
    Write-Host '            [-Output <path>] [-OutputFormat <json|csv>] [-NoBuild] [-ExtraArgs <args>]'
    Write-Host '            [-Baseline <before.json>] [-Candidate <after.json>]'
    Write-Host ''
    Write-Host 'Global execution option:'
    Write-Host '  -NoWorkspaceTemp              Preserve the caller TEMP and TMP values.'
    Write-Host ''
    Write-Host 'Use .\gamewip.bat list for valid IDs and the generated command-line tools manual for complete behavior.'
}
