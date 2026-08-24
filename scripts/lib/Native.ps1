# GameWIP Native helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function ConvertTo-GameWipNativeArgument
{
    param([AllowEmptyString()][string]$Argument)

    if ($null -eq $Argument)
    {
        return '""'
    }
    if ($Argument.Length -eq 0)
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
    $builder.ToString()
}

function ConvertTo-GameWipNativeCommandLine
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    (@(ConvertTo-GameWipNativeArgument $FilePath) + @($Arguments | ForEach-Object { ConvertTo-GameWipNativeArgument $_ })) -join ' '
}

function Invoke-GameWipNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [switch]$UseWorkspaceTemp,
        [string]$PathPrefix
    )

    Initialize-GameWipRunLog
    $commandLine = ConvertTo-GameWipNativeCommandLine -FilePath $FilePath -Arguments $Arguments
    $step = Initialize-GameWipToolRunStep -Run $Script:RunContext -Name $Name -CommandLine $commandLine
    $logPath = $step.LogPath

    Write-Host ''
    Write-Host "Starting: $Name" -ForegroundColor Cyan
    Write-Host "> $commandLine"
    Write-Host "  log: $logPath"

    $previousLocation = Get-Location
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $previousPath = $env:PATH
    $exitCode = 0
    $previousErrorActionPreference = $ErrorActionPreference

    try
    {
        Set-Location $RepositoryRoot
        $ErrorActionPreference = 'Continue'
        if ($UseWorkspaceTemp -and -not $NoWorkspaceTemp)
        {
            $env:TEMP = $Script:OperationTemp
            $env:TMP = $Script:OperationTemp
            Write-Host "  workspace temp: $Script:OperationTemp"
        }
        if (-not [string]::IsNullOrWhiteSpace($PathPrefix))
        {
            $env:PATH = @($PathPrefix, $env:PATH) -join [IO.Path]::PathSeparator
            Write-Host "  PATH prefix: $PathPrefix"
        }

        & $FilePath @Arguments 2>&1 | ForEach-Object { [string]$_ } | Tee-Object -FilePath $logPath
        $exitCode = if ($null -ne $LASTEXITCODE)
        {
            [int]$LASTEXITCODE
        }
        else
        {
            0
        }
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        $env:PATH = $previousPath
        Set-Location $previousLocation
    }

    Complete-GameWipToolRunStep -Run $Script:RunContext -Step $step -ExitCode $exitCode
    if ($exitCode -ne 0)
    {
        $Script:RunFailed = $true
        throw "$Name failed with exit code $exitCode. See $logPath"
    }
    Write-Host "Finished: $Name" -ForegroundColor Green
}
