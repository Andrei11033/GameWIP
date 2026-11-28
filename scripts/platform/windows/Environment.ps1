# Windows process environment helpers.

# ------------------------------------------------------------
# Process PATH refresh
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Update-GameWipProcessPath
{
    $entries = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($pathValue in @(
            $env:Path,
            [Environment]::GetEnvironmentVariable('Path', 'Machine'),
            [Environment]::GetEnvironmentVariable('Path', 'User')
        ))
    {
        foreach ($entry in @([string]$pathValue -split [IO.Path]::PathSeparator))
        {
            $trimmed = $entry.Trim().Trim('"')

            if (-not [string]::IsNullOrWhiteSpace($trimmed) -and $seen.Add($trimmed))
            {
                $entries.Add($trimmed) | Out-Null
            }
        }
    }

    $env:Path = $entries -join [IO.Path]::PathSeparator
}
