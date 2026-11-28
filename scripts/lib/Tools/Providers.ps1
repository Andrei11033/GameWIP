# ------------------------------------------------------------
# Toolchain validation and provider dispatch
# ------------------------------------------------------------

function Test-GameWipToolchain
{
    param(
        [AllowEmptyCollection()][string[]]$ToolIds = @(),
        [switch]$IncludeInformational,
        [switch]$DisplayStatus,
        [switch]$ThrowOnFailure,
        [string]$FailureCode = 'toolchain-incomplete',
        [string]$FailureSummary = 'The declared project toolchain is incomplete.',
        [string[]]$SuggestedActions = @(),
        [int]$Indent = 2
    )

    # Build recovery guidance when the caller did not provide it.
    if ($SuggestedActions.Count -eq 0)
    {
        $helperCommand = Get-GameWipProjectHelperCommand
        $SuggestedActions = @("$helperCommand tool ensure all -Yes", "$helperCommand tool status")
    }

    # Select requested tools that this host can check.
    $selected = if ($ToolIds.Count -ne 0)
    {
        @($ToolIds | ForEach-Object { Get-GameWipProjectTool -Id $_ })
    }
    else
    {
        @($ProjectTools.tools)
    }

    if (-not $IncludeInformational)
    {
        $selected = @($selected | Where-Object { $_.versionPolicy -ne 'informational' })
    }
    $selected = @($selected | Where-Object { Test-GameWipToolHostSupport -Tool $_ })
    $selected = @($selected | Where-Object { $_.capabilities.detectInstalled })

    $results = [System.Collections.Generic.List[object]]::new()
    $failures = [System.Collections.Generic.List[string]]::new()
    $toolPaths = @{}

    if ($DisplayStatus)
    {
        Write-Host "Checking $($selected.Count) declared tool(s)..."
    }

    # Collect tool and dependency results.
    for ($toolIndex = 0; $toolIndex -lt $selected.Count; ++$toolIndex)
    {
        $tool = $selected[$toolIndex]

        try
        {
            $status = Get-GameWipToolStatus -Tool $tool
        }

        catch
        {
            $status = [pscustomobject]@{
                Tool = $tool
                Detected = [pscustomobject]@{ Installed = $false; Version = $null; Location = $null; Source = $null; Candidates = @() }
                Compatibility = 'unknown'
                Dependencies = @()
                DependencyFailures = @()
                State = 'unknown'
                Ready = $false
                Error = $_.Exception.Message
            }
        }

        $results.Add($status) | Out-Null

        if ($status.Ready -and $status.Detected.Location)
        {
            $toolPaths[[string]$tool.id] = [string]$status.Detected.Location
        }

        if ($status.State -ne 'compatible' -and $status.State -ne 'dependency-missing')
        {
            $location = if ($status.Detected.Location)
            {
                [string]$status.Detected.Location
            }
            else
            {
                'not found'
            }
            $detail = if ($status.Error)
            {
                [string]$status.Error
            }
            else
            {
                $location
            }
            $failures.Add("$($tool.id): $($status.State) ($detail)") | Out-Null
        }

        foreach ($dependency in @($status.DependencyFailures))
        {
            $dependencyDetail = if ($dependency.Reason)
            {
                " - $($dependency.Reason)"
            }
            else
            {
                ''
            }
            $failures.Add("$($tool.id) dependency '$($dependency.Package)': $($dependency.State)$dependencyDetail") | Out-Null
        }

        if ($DisplayStatus)
        {
            $semantic = Get-GameWipToolCompatibilitySemantic -Compatibility $status.State
            Write-GameWipStatusLine `
                -Status "$($toolIndex + 1)/$($selected.Count)" `
                -Text ([string]$tool.id) `
                -Suffix "($($status.State))" `
                -Semantic $semantic `
                -SuffixSemantic Muted `
                -Indent $Indent
        }
    }

    # Summarize the selected tool results.
    $result = [pscustomobject]@{
        Ready = $failures.Count -eq 0
        Results = @($results)
        Failures = @($failures)
        Tools = $toolPaths
    }

    if (-not $result.Ready -and $ThrowOnFailure)
    {
        throw (New-GameWipDiagnosticException `
                -Code $FailureCode `
                -Summary $FailureSummary `
                -Details ($failures -join "`n") `
                -SuggestedActions $SuggestedActions)
    }

    return $result
}

function Test-GameWipDetectedToolFromDeclaredProvider
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool, [Parameter(Mandatory = $true)]$Detected)

    if (-not $Detected.Installed)
    {
        return $false
    }

    switch ([string]$Tool.provider.kind)
    {
        'python'
        {
            return [string]$Detected.Source -like 'GameWIPTools python*'
        }

        'npm'
        {
            return [string]$Detected.Source -like 'GameWIPTools npm*'
        }

        'githubRelease'
        {
            return [string]$Detected.Source -like 'GameWIPTools githubRelease*'
        }

        'archive'
        {
            return [string]$Detected.Source -like 'GameWIPTools archive*'
        }

        'powershellGallery'
        {
            return [string]$Detected.Source -like 'GameWIPTools powershellGallery*'
        }
        'external'
        {
            if ($Tool.provider.Contains('managedPath'))
            {
                return [string]$Detected.Source -eq 'managed environment'
            }
            return $true
        }
        default
        {
            return $true
        }
    }
}

function Get-GameWipProviderFunction
{
    param([hashtable]$Tool, [ValidateSet('Latest', 'Install')][string]$Operation)
    $providerName = switch ([string]$Tool.provider.kind)
    {
        'msys2'
        {
            'Msys2'
        }
        'npm'
        {
            'Npm'
        }
        'python'
        {
            'Python'
        }
        'powershellGallery'
        {
            'PowerShellGallery'
        }
        'githubRelease'
        {
            'GitHubRelease'
        }
        'archive'
        {
            'Archive'
        }
        'winget'
        {
            'Winget'
        }
        'external'
        {
            'External'
        }
        default
        {
            throw "Unsupported tool provider '$($Tool.provider.kind)'."
        }
    }

    return $(if ($Operation -eq 'Latest')
        {
            "Get-GameWip${providerName}ToolLatestVersion"
        }
        else
        {
            "Install-GameWip${providerName}Tool"
        })
}

function Invoke-GameWipProviderNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$PathPrefix,
        [int[]]$AllowedExitCodes = @(0),
        [int]$TimeoutSeconds = 600
    )
    $result = Invoke-GameWipNative `
        -Name $Name `
        -FilePath $FilePath `
        -Arguments $Arguments `
        -PathPrefix $PathPrefix `
        -AllowedExitCodes $AllowedExitCodes `
        -TimeoutSeconds $TimeoutSeconds
    return [int]$result.ExitCode
}
