# GameWIP bounded read-only HTTP policy for metadata queries and downloads.

Set-StrictMode -Version Latest

function Test-GameWipTransientNetworkError
{
    param([Parameter(Mandatory = $true)]$ErrorRecord)
    try
    {
        $response = $ErrorRecord.Exception.Response
        if ($null -eq $response)
        {
            return $true
        }
        $statusCode = [int]$response.StatusCode
        return $statusCode -eq 408 -or $statusCode -eq 429 -or $statusCode -ge 500
    }
    catch
    {
        return $true
    }
}

function Invoke-GameWipHttpDownloadAttempt
{
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$OutFile,
        [hashtable]$Headers = @{},
        [ValidateRange(1, 300)][int]$TimeoutSeconds = 60,
        [string]$Label = 'Download'
    )

    Add-Type -AssemblyName System.Net.Http
    $client = $null
    $response = $null
    $inputStream = $null
    $outputStream = $null
    try
    {
        $client = [Net.Http.HttpClient]::new()
        $client.Timeout = [TimeSpan]::FromSeconds($TimeoutSeconds)
        foreach ($name in $Headers.Keys)
        {
            [void]$client.DefaultRequestHeaders.TryAddWithoutValidation([string]$name, [string]$Headers[$name])
        }

        $responseTask = $client.GetAsync($Uri, [Net.Http.HttpCompletionOption]::ResponseHeadersRead)
        $lastReport = [Diagnostics.Stopwatch]::StartNew()
        while (-not $responseTask.IsCompleted)
        {
            Assert-GameWipNotCancelled
            if ($lastReport.Elapsed.TotalSeconds -ge 15)
            {
                Write-GameWipHost "  $Label is still connecting..." -ForegroundColor DarkGray
                $lastReport.Restart()
            }
            Start-Sleep -Milliseconds 200
        }
        $response = $responseTask.GetAwaiter().GetResult()
        [void]$response.EnsureSuccessStatusCode()
        $contentLength = $response.Content.Headers.ContentLength
        $inputStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $outputStream = [IO.File]::Open($OutFile, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $buffer = [byte[]]::new(65536)
        [long]$received = 0
        $lastReport.Restart()
        while ($true)
        {
            Assert-GameWipNotCancelled
            $readTask = $inputStream.ReadAsync($buffer, 0, $buffer.Length)
            while (-not $readTask.IsCompleted)
            {
                Assert-GameWipNotCancelled
                if ($lastReport.Elapsed.TotalSeconds -ge 15)
                {
                    Write-GameWipHost "  $Label is still downloading ($([Math]::Round($received / 1MB, 1)) MiB received)..." -ForegroundColor DarkGray
                    $lastReport.Restart()
                }
                Start-Sleep -Milliseconds 200
            }
            $read = $readTask.GetAwaiter().GetResult()
            if ($read -eq 0)
            {
                break
            }
            $outputStream.Write($buffer, 0, $read)
            $received += $read
            if ($lastReport.Elapsed.TotalSeconds -ge 15)
            {
                $progress = if ($null -ne $contentLength -and $contentLength -gt 0)
                {
                    '{0:N1}/{1:N1} MiB ({2:N0}%)' -f ($received / 1MB), ($contentLength / 1MB), (($received * 100.0) / $contentLength)
                }
                else
                {
                    '{0:N1} MiB received' -f ($received / 1MB)
                }
                Write-GameWipHost "  $Label download progress: $progress" -ForegroundColor DarkGray
                $lastReport.Restart()
            }
        }
    }
    finally
    {
        if ($null -ne $outputStream)
        {
            $outputStream.Dispose()
        }
        if ($null -ne $inputStream)
        {
            $inputStream.Dispose()
        }
        if ($null -ne $response)
        {
            $response.Dispose()
        }
        if ($null -ne $client)
        {
            $client.Dispose()
        }
    }
}

function Invoke-GameWipHttpRead
{
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [hashtable]$Headers = @{},
        [ValidateRange(1, 10)][int]$MaxAttempts = 3,
        [ValidateRange(1, 300)][int]$TimeoutSeconds = 30,
        [switch]$Json,
        [string]$OutFile,
        [string]$Label = 'Download'
    )

    $lastError = $null
    $attemptsUsed = 0
    for ($attempt = 1; $attempt -le $MaxAttempts; ++$attempt)
    {
        $attemptsUsed = $attempt
        Assert-GameWipNotCancelled
        try
        {
            Write-Verbose "HTTP read attempt $attempt/${MaxAttempts}: $Uri"
            if (-not [string]::IsNullOrWhiteSpace($OutFile))
            {
                $fullOutFile = [IO.Path]::GetFullPath($OutFile)
                $parent = Split-Path -Parent $fullOutFile

                if (-not (Test-Path -LiteralPath $parent -PathType Container))
                {
                    Set-GameWipMutationStarted
                    New-Item -ItemType Directory -Force -Path $parent | Out-Null
                }

                $temporaryOutFile = Join-Path $parent (
                    '.{0}.{1}.download' -f
                    [IO.Path]::GetFileName($fullOutFile),
                    [guid]::NewGuid().ToString('N')
                )

                try
                {
                    Invoke-GameWipHttpDownloadAttempt `
                        -Uri $Uri `
                        -OutFile $temporaryOutFile `
                        -Headers $Headers `
                        -TimeoutSeconds $TimeoutSeconds `
                        -Label $Label

                    Set-GameWipMutationStarted
                    Move-Item -LiteralPath $temporaryOutFile -Destination $fullOutFile -Force
                    $temporaryOutFile = $null

                    return [pscustomobject]@{
                        State = 'resolved'
                        Value = $fullOutFile
                        Attempts = $attempt
                        Reason = ''
                    }
                }
                finally
                {
                    if (-not [string]::IsNullOrWhiteSpace([string]$temporaryOutFile))
                    {
                        Remove-Item -LiteralPath $temporaryOutFile -Force -ErrorAction SilentlyContinue
                    }
                }
            }
            if ($Json)
            {
                $value = Invoke-RestMethod -Uri $Uri -Headers $Headers -TimeoutSec $TimeoutSeconds
            }
            else
            {
                $value = Invoke-WebRequest -Uri $Uri -Headers $Headers -TimeoutSec $TimeoutSeconds -UseBasicParsing
            }
            return [pscustomobject]@{ State = 'resolved'; Value = $value; Attempts = $attempt; Reason = '' }
        }
        catch
        {
            $lastError = $_
            $transient = Test-GameWipTransientNetworkError -ErrorRecord $_
            if (-not $transient -or $attempt -ge $MaxAttempts)
            {
                break
            }
            $delaySeconds = [Math]::Min(8, [Math]::Pow(2, $attempt - 1))
            Write-GameWipHost ("  retry {0}/{1} in {2}s: {3}" -f ($attempt + 1), $MaxAttempts, $delaySeconds, $_.Exception.Message) -ForegroundColor Yellow
            Start-Sleep -Seconds $delaySeconds
        }
    }

    $reason = if ($null -ne $lastError)
    {
        $lastError.Exception.Message
    }
    else
    {
        'request did not complete'
    }
    return [pscustomobject]@{ State = 'unavailable'; Value = $null; Attempts = $attemptsUsed; Reason = $reason }
}

function Invoke-GameWipHttpJson
{
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [hashtable]$Headers = @{},
        [ValidateRange(1, 10)][int]$MaxAttempts = 3,
        [ValidateRange(1, 300)][int]$TimeoutSeconds = 30
    )
    return Invoke-GameWipHttpRead -Uri $Uri -Headers $Headers -MaxAttempts $MaxAttempts -TimeoutSeconds $TimeoutSeconds -Json
}

function Invoke-GameWipDownload
{
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$OutFile,
        [hashtable]$Headers = @{},
        [ValidateRange(1, 10)][int]$MaxAttempts = 3,
        [ValidateRange(1, 300)][int]$TimeoutSeconds = 60,
        [string]$Label = 'Download'
    )
    return Invoke-GameWipHttpRead -Uri $Uri -Headers $Headers -MaxAttempts $MaxAttempts -TimeoutSeconds $TimeoutSeconds -OutFile $OutFile -Label $Label
}
