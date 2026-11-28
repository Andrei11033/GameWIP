# Native command construction and launch preparation.

# ------------------------------------------------------------
# Command-line and process launch preparation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Native command construction
# ------------------------------------------------------------

function ConvertTo-GameWipNativeArgument
{
    param([AllowEmptyString()][string]$Argument)

    if ($null -eq $Argument -or $Argument.Length -eq 0)
    {
        return '""'
    }

    if ($Argument -notmatch '[\s"&|<>^()%!]')
    {
        return $Argument
    }

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0

    foreach ($character in $Argument.ToCharArray())
    {
        if ($character -eq '\')
        {
            ++$backslashes
            continue
        }

        if ($character -eq '"')
        {
            [void]$builder.Append('\' * (($backslashes * 2) + 1))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }

        if ($backslashes -gt 0)
        {
            [void]$builder.Append('\' * $backslashes)
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }

    if ($backslashes -gt 0)
    {
        [void]$builder.Append('\' * ($backslashes * 2))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-GameWipNativeCommandLine
{
    param([Parameter(Mandatory = $true)][string]$FilePath, [string[]]$Arguments = @())
    return (@(ConvertTo-GameWipNativeArgument $FilePath) + @($Arguments | ForEach-Object { ConvertTo-GameWipNativeArgument $_ })) -join ' '
}

function Resolve-GameWipProcessLaunch
{
    param([Parameter(Mandatory = $true)][string]$FilePath, [string[]]$Arguments = @())

    $extension = [IO.Path]::GetExtension($FilePath).ToLowerInvariant()

    if ((Test-GameWipWindowsHost) -and $extension -in @('.cmd', '.bat'))
    {
        $commandInterpreter = if ([string]::IsNullOrWhiteSpace([string]$env:ComSpec))
        {
            'cmd.exe'
        }
        else
        {
            [string]$env:ComSpec
        }
        $commandLine = ConvertTo-GameWipNativeCommandLine -FilePath $FilePath -Arguments $Arguments
        return [pscustomobject]@{ FilePath = $commandInterpreter; ArgumentLine = "/d /s /c `"$commandLine`"" }
    }

    if ((Test-GameWipWindowsHost) -and $extension -eq '.ps1')
    {
        $powerShell = (Get-Process -Id $PID).Path
        $launchArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $FilePath) + @($Arguments)
        $argumentLine = @(
            $launchArguments | ForEach-Object { ConvertTo-GameWipNativeArgument $_ }
        ) -join ' '

        return [pscustomobject]@{
            FilePath = $powerShell
            ArgumentLine = $argumentLine
        }
    }

    return [pscustomobject]@{
        FilePath = $FilePath
        ArgumentLine = (@($Arguments | ForEach-Object { ConvertTo-GameWipNativeArgument $_ }) -join ' ')
    }
}

function Resolve-GameWipOwnedProcessLaunch
{
    param([Parameter(Mandatory = $true)][string]$FilePath, [string[]]$Arguments = @())

    $launch = Resolve-GameWipProcessLaunch -FilePath $FilePath -Arguments $Arguments

    if (Test-GameWipWindowsHost)
    {
        return [pscustomobject]@{
            FilePath = $launch.FilePath
            ArgumentLine = $launch.ArgumentLine
            OwnProcessGroup = $false
        }
    }

    $setsid = Get-Command -Name setsid -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1

    if ($null -eq $setsid)
    {
        return [pscustomobject]@{
            FilePath = $launch.FilePath
            ArgumentLine = $launch.ArgumentLine
            OwnProcessGroup = $false
        }
    }

    $argumentLine = ConvertTo-GameWipNativeArgument -Argument $launch.FilePath

    if (-not [string]::IsNullOrWhiteSpace([string]$launch.ArgumentLine))
    {
        $argumentLine += " $($launch.ArgumentLine)"
    }

    return [pscustomobject]@{
        FilePath = $setsid.Source
        ArgumentLine = $argumentLine
        OwnProcessGroup = $true
    }
}
