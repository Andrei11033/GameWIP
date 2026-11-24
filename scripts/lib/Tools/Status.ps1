# ------------------------------------------------------------
# Compatibility and dependency status
# ------------------------------------------------------------

# Compare detected versions with the tool's declared policy.

function Get-GameWipToolCompatibility
{
    param([hashtable]$Tool, $Detected)

    if (-not $Detected.Installed)
    {
        return 'missing'
    }

    if ($Tool.versionPolicy -in @('managed', 'informational'))
    {
        return 'compatible'
    }

    if ([string]::IsNullOrWhiteSpace([string]$Detected.Version))
    {
        return 'unknown'
    }

    if ($Tool.versionPolicy -eq 'exact')
    {
        if ([string]$Detected.Version -eq [string]$Tool.requiredVersion)
        {
            return 'compatible'
        }

        return 'mismatch'
    }

    try
    {
        if ([version]$Detected.Version -ge [version]$Tool.requiredVersion)
        {
            return 'compatible'
        }

        return 'outdated'
    }

    catch
    {
        return 'unknown'
    }
}

function Get-GameWipToolCompatibilitySemantic
{
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('compatible', 'missing', 'mismatch', 'outdated', 'unknown', 'dependency-missing')]
        [string]$Compatibility
    )

    switch ($Compatibility)
    {
        'compatible'
        {
            return 'Success'
        }

        { $_ -in @('missing', 'dependency-missing') }
        {
            return 'Failure'
        }
        default
        {
            return 'Warning'
        }
    }
}

function Get-GameWipToolProviderDependencyStatus
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    # Skip Windows package checks when Linux owns these dependencies.
    if (-not (Test-GameWipWindowsHost) -and [string]$Tool.provider.kind -eq 'msys2')
    {
        return @()
    }

    if ((Test-GameWipWindowsHost) -and
        [string]$Tool.provider.kind -eq 'python' -and
        $Tool.provider.Contains('windowsMsys2'))
    {
        $nativeProvider = $Tool.provider.windowsMsys2
        $nativeTool = @{
            provider = @{
                dependencies = @($nativeProvider)
            }
        }
        return @(Get-GameWipMsys2ToolDependencyStatus -Tool $nativeTool)
    }

    # Read the provider's declared dependencies.
    $dependencies = @(
        if ($Tool.provider.Contains('dependencies'))
        {
            @($Tool.provider.dependencies)
        }
        else
        {
            @()
        }
    )

    if ($dependencies.Count -eq 0)
    {
        return @()
    }

    # Use the provider checker when one is available.
    $functionName = switch ([string]$Tool.provider.kind)
    {
        'msys2'
        {
            'Get-GameWipMsys2ToolDependencyStatus'
        }
        'npm'
        {
            'Get-GameWipNpmToolDependencyStatus'
        }
        default
        {
            $null
        }
    }

    if ([string]::IsNullOrWhiteSpace($functionName))
    {
        $dependencyStatusError = "Provider '$($Tool.provider.kind)' does not implement dependency checking."
    }
    else
    {
        try
        {
            return @(& $functionName -Tool $Tool)
        }

        catch
        {
            $dependencyStatusError = $_.Exception.Message
        }
    }

    return @($dependencies | ForEach-Object {
            [pscustomobject]@{
                Package = [string]$_.package
                RequiredVersion = if ($_.Contains('version'))
                {
                    [string]$_.version
                }
                else
                {
                    $null
                }
                InstalledVersion = $null
                State = 'unknown'
                Location = $null
                Reason = $dependencyStatusError
            }
        })
}

function Get-GameWipToolStatus
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $detected = Get-GameWipDetectedTool -Tool $Tool
    $compatibility = Get-GameWipToolCompatibility -Tool $Tool -Detected $detected
    $dependencies = @(Get-GameWipToolProviderDependencyStatus -Tool $Tool)
    $dependencyFailures = @($dependencies | Where-Object { $_.State -ne 'compatible' })

    # Combine tool and dependency compatibility into one state.
    $state = if ($compatibility -ne 'compatible')
    {
        $compatibility
    }
    elseif ($dependencyFailures.Count -ne 0)
    {
        'dependency-missing'
    }
    else
    {
        'compatible'
    }

    return [pscustomobject]@{
        Tool = $Tool
        Detected = $detected
        Compatibility = $compatibility
        Dependencies = $dependencies
        DependencyFailures = $dependencyFailures
        State = $state
        Ready = $state -eq 'compatible'
        Error = $null
    }
}
