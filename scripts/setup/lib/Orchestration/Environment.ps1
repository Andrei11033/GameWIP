# ------------------------------------------------------------
# Environment verification
# ------------------------------------------------------------

function Invoke-GameWipSetupEnvironmentCheck
{
    Write-GameWipSection 'Environment check'
    $failures = [System.Collections.Generic.List[string]]::new()

    # Check persisted setup state and required project tools.
    $null = Get-GameWipSetupState

    if ($script:SetupStateReadStatus -in @('needs-repair', 'corrupt'))
    {
        $repairCommand = "$(Get-GameWipSetupHelperCommand) repair"
        $stateIssue = if ($script:SetupStateReadStatus -eq 'corrupt')
        {
            'is unreadable and will be rebuilt'
        }
        else
        {
            'is incomplete and will be normalized'
        }

        $failures.Add("Setup state $stateIssue. Run '$repairCommand'.") | Out-Null
    }

    $target = Get-GameWipSelectedBuildTarget
    $toolIds = @(Get-GameWipProjectReadinessToolIds -Target $target)
    $toolchain = Test-GameWipToolchain -ToolIds $toolIds -DisplayStatus -Indent 4

    foreach ($failure in @($toolchain.Failures))
    {
        $failures.Add("Project toolchain: $failure") | Out-Null
    }

    # Check profiler tools and checkout state when applicable.
    if (Test-GameWipWindowsHost)
    {
        Write-Host '  Checking matching Tracy tools...'

        if (-not (Test-GameWipTracyToolSet))
        {
            $failures.Add('Matching Tracy profiler tools are not installed.') | Out-Null
        }
    }
    else
    {
        Write-Host '  Checking matching Tracy profiler...'

        if (-not (Test-GameWipLinuxTracyProfiler))
        {
            $failures.Add('Matching Tracy profiler is not installed.') | Out-Null
        }
    }
    Write-Host '  Checking repository and editor integration...'

    try
    {
        Test-GameWipRepositoryState -RepositoryRoot $RepositoryRoot
    }

    catch
    {
        $failures.Add($_.Exception.Message) | Out-Null
    }

    # Verify only integrations for editors that are installed.
    $selected = @(Get-GameWipEditorSelection -EditorConfig $EditorConfig)

    $codeInstalled = Test-GameWipSetupCommand -Name 'code'
    $editorsToCheck = [System.Collections.Generic.List[string]]::new()

    foreach ($id in $selected)
    {
        $option = $EditorConfig.Options | Where-Object { $_.Id -eq $id } | Select-Object -First 1

        if ($option.Handler -eq 'vscode')
        {
            if ($codeInstalled)
            {
                $editorsToCheck.Add($id) | Out-Null
            }
            else
            {
                Write-Host '  VS Code is not installed; its integration checks are skipped.'
            }

            continue
        }

        if ($option.Handler -eq 'visual-studio' -and (Test-GameWipWindowsHost))
        {
            if (Get-GameWipVisualStudioInstallation)
            {
                $editorsToCheck.Add($id) | Out-Null
            }
            else
            {
                Write-Host '  Visual Studio is not installed; its integration checks are skipped.'
            }

            continue
        }

        $editorsToCheck.Add($id) | Out-Null
    }

    foreach ($failure in @(Get-GameWipEditorFailure -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $editorsToCheck.ToArray()))
    {
        $failures.Add($failure) | Out-Null
    }

    # Check the locked dependency cache.
    Write-Host '  Checking GameWIP dependency cache...'

    try
    {
        Test-GameWipDependencyCache -ThrowOnFailure | Out-Null
    }

    catch
    {
        $failures.Add($_.Exception.Message) | Out-Null
    }

    # Report all failures together.
    if ($failures.Count -ne 0)
    {
        foreach ($failure in $failures)
        {
            Write-GameWipStatusLine -Status FAIL -Text $failure -Semantic Failure -Indent 2
        }
        $repairCommand = "$(Get-GameWipSetupHelperCommand) repair"
        throw "$($failures.Count) environment check(s) failed. Run '$repairCommand'."
    }

    Write-GameWipStatusLine -Status OK -Text 'Complete selected development environment is ready.' -Semantic Success -Indent 2
}
