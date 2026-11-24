# Owned process launch, output, and registration.

# ------------------------------------------------------------
# Owned process execution
# ------------------------------------------------------------

function New-GameWipProcessOutputReader
{
    param([Parameter(Mandatory = $true)][string]$Path)

    return [pscustomobject]@{
        Path = $Path
        Stream = $null
        Decoder = $null
        ByteBuffer = [byte[]]::new(4096)
        CharacterBuffer = [char[]]::new(8192)
        InitialBytes = [System.Collections.Generic.List[byte]]::new()
        InitialSkipBytes = 0
        PendingLine = [System.Text.StringBuilder]::new()
        PendingCarriageReturn = $false
        Closed = $false
    }
}

function Write-GameWipProcessOutputLine
{
    param(
        [Parameter(Mandatory = $true)][string]$Line,
        [string]$Prefix = '',
        [string]$PrefixColor
    )

    $colorEnabled = (
        -not [string]::IsNullOrWhiteSpace($PrefixColor) -and
        ($null -eq $Script:OperationContext -or -not $Script:OperationContext.NoColor)
    )

    if ($colorEnabled -and -not [string]::IsNullOrEmpty($Prefix))
    {
        Write-GameWipHost $Prefix -NoNewline -ForegroundColor $PrefixColor
        Write-Host $Line
    }
    else
    {
        Write-Host "$Prefix$Line"
    }
}

function Initialize-GameWipProcessOutputDecoder
{
    param(
        [Parameter(Mandatory = $true)]$ReaderState,
        [switch]$Flush
    )

    $bytes = $ReaderState.InitialBytes
    $byteCount = $bytes.Count
    $encoding = [System.Text.Encoding]::Default
    $skipBytes = 0

    # Wait only when the current prefix could still be an encoding marker.
    if ($byteCount -eq 0)
    {
        if (-not $Flush)
        {
            return $false
        }
    }

    elseif ($bytes[0] -eq 0xEF)
    {
        if ($byteCount -lt 2 -and -not $Flush)
        {
            return $false
        }

        if ($byteCount -ge 2 -and $bytes[1] -eq 0xBB)
        {
            if ($byteCount -lt 3 -and -not $Flush)
            {
                return $false
            }

            if ($byteCount -ge 3 -and $bytes[2] -eq 0xBF)
            {
                $encoding = [System.Text.Encoding]::UTF8
                $skipBytes = 3
            }
        }
    }

    elseif ($bytes[0] -eq 0xFF)
    {
        if ($byteCount -lt 2 -and -not $Flush)
        {
            return $false
        }

        if ($byteCount -ge 2 -and $bytes[1] -eq 0xFE)
        {
            if ($byteCount -lt 4 -and -not $Flush)
            {
                return $false
            }

            if ($byteCount -ge 4 -and $bytes[2] -eq 0x00 -and $bytes[3] -eq 0x00)
            {
                $encoding = [System.Text.UTF32Encoding]::new($false, $true)
                $skipBytes = 4
            }

            else
            {
                $encoding = [System.Text.Encoding]::Unicode
                $skipBytes = 2
            }
        }
    }

    elseif ($bytes[0] -eq 0xFE)
    {
        if ($byteCount -lt 2 -and -not $Flush)
        {
            return $false
        }

        if ($byteCount -ge 2 -and $bytes[1] -eq 0xFF)
        {
            $encoding = [System.Text.Encoding]::BigEndianUnicode
            $skipBytes = 2
        }
    }

    elseif ($bytes[0] -eq 0x00)
    {
        if ($byteCount -lt 2 -and -not $Flush)
        {
            return $false
        }

        if ($byteCount -ge 2 -and $bytes[1] -eq 0x00)
        {
            if ($byteCount -lt 3 -and -not $Flush)
            {
                return $false
            }

            if ($byteCount -ge 3 -and $bytes[2] -eq 0xFE)
            {
                if ($byteCount -lt 4 -and -not $Flush)
                {
                    return $false
                }

                if ($byteCount -ge 4 -and $bytes[3] -eq 0xFF)
                {
                    $encoding = [System.Text.UTF32Encoding]::new($true, $true)
                    $skipBytes = 4
                }
            }
        }
    }

    $ReaderState.Decoder = $encoding.GetDecoder()
    $ReaderState.InitialSkipBytes = $skipBytes
    return $true
}

function Write-GameWipProcessDecodedOutput
{
    param(
        [Parameter(Mandatory = $true)]$ReaderState,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][int]$Count,
        [string]$Prefix = '',
        [string]$PrefixColor,
        [switch]$FlushDecoder
    )

    $charactersRead = $ReaderState.Decoder.GetChars(
        $Bytes,
        $Offset,
        $Count,
        $ReaderState.CharacterBuffer,
        0,
        [bool]$FlushDecoder
    )
    $emittedLines = 0

    # Split complete lines while retaining a trailing carriage return or text.
    for ($index = 0; $index -lt $charactersRead; ++$index)
    {
        $character = $ReaderState.CharacterBuffer[$index]

        if ($ReaderState.PendingCarriageReturn)
        {
            Write-GameWipProcessOutputLine -Line $ReaderState.PendingLine.ToString() -Prefix $Prefix -PrefixColor $PrefixColor
            $null = $ReaderState.PendingLine.Clear()
            $ReaderState.PendingCarriageReturn = $false
            ++$emittedLines

            if ($character -eq "`n")
            {
                continue
            }
        }

        if ($character -eq "`r")
        {
            $ReaderState.PendingCarriageReturn = $true
        }

        elseif ($character -eq "`n")
        {
            Write-GameWipProcessOutputLine -Line $ReaderState.PendingLine.ToString() -Prefix $Prefix -PrefixColor $PrefixColor
            $null = $ReaderState.PendingLine.Clear()
            ++$emittedLines
        }

        else
        {
            $null = $ReaderState.PendingLine.Append($character)
        }
    }

    return $emittedLines
}

function Write-GameWipProcessNewOutput
{
    param(
        [Parameter(Mandatory = $true)]$ReaderState,
        [string]$Prefix = '',
        [string]$PrefixColor,
        [switch]$Flush
    )

    if ($ReaderState.Closed)
    {
        return 0
    }

    # Open each stream once and retain its position while the process writes.
    if ($null -eq $ReaderState.Stream)
    {
        if (-not (Test-Path -LiteralPath $ReaderState.Path -PathType Leaf))
        {
            return 0
        }

        $stream = $null

        try
        {
            $stream = [System.IO.FileStream]::new(
                $ReaderState.Path,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read,
                ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
            )
            $ReaderState.Stream = $stream
        }

        catch [System.IO.IOException]
        {
            if ($null -ne $stream)
            {
                $stream.Dispose()
            }

            return 0
        }

        catch [System.UnauthorizedAccessException]
        {
            if ($null -ne $stream)
            {
                $stream.Dispose()
            }

            return 0
        }
    }

    $emittedLines = 0

    # Read only newly appended bytes and keep decoder state across polls.
    while ($true)
    {
        try
        {
            $bytesRead = $ReaderState.Stream.Read($ReaderState.ByteBuffer, 0, $ReaderState.ByteBuffer.Length)
        }

        catch [System.IO.IOException]
        {
            break
        }

        if ($bytesRead -eq 0)
        {
            break
        }

        if ($null -eq $ReaderState.Decoder)
        {
            for ($index = 0; $index -lt $bytesRead; ++$index)
            {
                $ReaderState.InitialBytes.Add($ReaderState.ByteBuffer[$index])
            }

            if (-not (Initialize-GameWipProcessOutputDecoder -ReaderState $ReaderState))
            {
                continue
            }

            $initialBytes = [byte[]]$ReaderState.InitialBytes.ToArray()
            $initialCount = $initialBytes.Length - $ReaderState.InitialSkipBytes

            if ($initialCount -gt 0)
            {
                $emittedLines += Write-GameWipProcessDecodedOutput `
                    -ReaderState $ReaderState `
                    -Bytes $initialBytes `
                    -Offset $ReaderState.InitialSkipBytes `
                    -Count $initialCount `
                    -Prefix $Prefix `
                    -PrefixColor $PrefixColor
            }

            $ReaderState.InitialBytes.Clear()
        }

        else
        {
            $emittedLines += Write-GameWipProcessDecodedOutput `
                -ReaderState $ReaderState `
                -Bytes $ReaderState.ByteBuffer `
                -Offset 0 `
                -Count $bytesRead `
                -Prefix $Prefix `
                -PrefixColor $PrefixColor
        }
    }

    # Flush the final decoder sequence and any unterminated output after exit.
    if ($Flush)
    {
        if ($null -eq $ReaderState.Decoder)
        {
            $null = Initialize-GameWipProcessOutputDecoder -ReaderState $ReaderState -Flush

            $initialBytes = [byte[]]$ReaderState.InitialBytes.ToArray()
            $initialCount = $initialBytes.Length - $ReaderState.InitialSkipBytes

            if ($initialCount -gt 0)
            {
                $emittedLines += Write-GameWipProcessDecodedOutput `
                    -ReaderState $ReaderState `
                    -Bytes $initialBytes `
                    -Offset $ReaderState.InitialSkipBytes `
                    -Count $initialCount `
                    -Prefix $Prefix `
                    -PrefixColor $PrefixColor
            }

            $ReaderState.InitialBytes.Clear()
        }

        $emittedLines += Write-GameWipProcessDecodedOutput `
            -ReaderState $ReaderState `
            -Bytes ([byte[]]::new(0)) `
            -Offset 0 `
            -Count 0 `
            -Prefix $Prefix `
            -PrefixColor $PrefixColor `
            -FlushDecoder

        if ($ReaderState.PendingCarriageReturn)
        {
            Write-GameWipProcessOutputLine -Line $ReaderState.PendingLine.ToString() -Prefix $Prefix -PrefixColor $PrefixColor
            $null = $ReaderState.PendingLine.Clear()
            $ReaderState.PendingCarriageReturn = $false
            ++$emittedLines
        }

        elseif ($ReaderState.PendingLine.Length -ne 0)
        {
            Write-GameWipProcessOutputLine -Line $ReaderState.PendingLine.ToString() -Prefix $Prefix -PrefixColor $PrefixColor
            $null = $ReaderState.PendingLine.Clear()
            ++$emittedLines
        }
    }

    return $emittedLines
}

function Close-GameWipProcessOutputReader
{
    param([Parameter(Mandatory = $true)]$ReaderState)

    if ($null -ne $ReaderState.Stream)
    {
        try
        {
            $ReaderState.Stream.Dispose()
        }

        catch [System.IO.IOException]
        {
            $null = $_.Exception
        }

        finally
        {
            $ReaderState.Stream = $null
        }
    }

    $ReaderState.Closed = $true
}

function Start-GameWipOwnedProcess
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $RepositoryRoot,
        [string]$RedirectStandardOutput,
        [string]$RedirectStandardError
    )
    Assert-GameWipNotCancelled
    $launch = Resolve-GameWipOwnedProcessLaunch -FilePath $FilePath -Arguments $Arguments
    $start = @{
        FilePath = $launch.FilePath
        WorkingDirectory = $WorkingDirectory
        PassThru = $true
        NoNewWindow = $true
    }

    if (-not [string]::IsNullOrWhiteSpace([string]$launch.ArgumentLine))
    {
        $start.ArgumentList = $launch.ArgumentLine
    }

    if ($RedirectStandardOutput)
    {
        $start.RedirectStandardOutput = $RedirectStandardOutput
    }

    if ($RedirectStandardError)
    {
        $start.RedirectStandardError = $RedirectStandardError
    }

    $process = Start-Process @start
    # Acquire the native handle before a short-lived child can exit. Without
    # this, Windows PowerShell can report ExitCode = 0 for a failed process.
    $null = $process.Handle
    $process | Add-Member -NotePropertyName GameWipOwnProcessGroup -NotePropertyValue ([bool]$launch.OwnProcessGroup)

    if ($null -ne $Script:OperationContext)
    {
        $Script:OperationContext.ActiveProcesses.Add([pscustomobject]@{
                # The registration supports cancellation and final cleanup.
                Process = $process
                Command = ConvertTo-GameWipNativeCommandLine -FilePath $FilePath -Arguments $Arguments
            }) | Out-Null
    }

    return $process
}

function Remove-GameWipOwnedProcessRegistration
{
    param([Parameter(Mandatory = $true)]$Process)

    if ($null -eq $Script:OperationContext)
    {
        return
    }

    for ($index = $Script:OperationContext.ActiveProcesses.Count - 1; $index -ge 0; --$index)
    {
        if ($Script:OperationContext.ActiveProcesses[$index].Process.Id -eq $Process.Id)
        {
            $Script:OperationContext.ActiveProcesses.RemoveAt($index)
        }
    }
}
