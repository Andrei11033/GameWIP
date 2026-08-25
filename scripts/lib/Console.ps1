# GameWIP console input and rendering primitives. No operation dispatch belongs here.

Set-StrictMode -Version Latest

function Write-GameWipSection
{
    param([Parameter(Mandatory = $true)][string]$Title)
    Write-Host ''
    Write-Host $Title
    Write-Host ('=' * $Title.Length)
}

function Test-GameWipInteractiveConsole
{
    try
    {
        return -not [Console]::IsInputRedirected -and -not [Console]::IsOutputRedirected -and $Host.Name -ne 'ServerRemoteHost'
    }
    catch
    {
        return $false
    }
}

function Assert-GameWipInteractiveConsole
{
    param([string]$Purpose = 'This action')
    if (-not (Test-GameWipInteractiveConsole))
    {
        throw (New-GameWipDiagnosticException `
                -Code 'interactive-console-required' `
                -Summary "$Purpose requires an interactive console." `
                -SuggestedActions @('Use the corresponding explicit CLI command instead of an interactive menu.', 'Do not pipe or redirect stdin for interactive helper flows.'))
    }
}

function New-GameWipChoiceResult
{
    param(
        [ValidateSet('Selected', 'Cancelled')][string]$Status,
        [AllowNull()]$Value
    )
    [pscustomobject]@{ Status = $Status; Value = $Value }
}


function Read-GameWipConsoleKey
{
    param([Parameter(Mandatory = $true)][string]$Purpose)
    Assert-GameWipInteractiveConsole -Purpose $Purpose
    return [Console]::ReadKey($true)
}

function Read-GameWipActionKey
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$AllowedKeys,
        [switch]$AllowEscape
    )
    while ($true)
    {
        Write-Host "$Prompt " -NoNewline
        $key = Read-GameWipConsoleKey -Purpose $Prompt
        if ($AllowEscape -and $key.Key -eq [ConsoleKey]::Escape)
        {
            Write-Host 'ESC'
            return New-GameWipChoiceResult -Status Cancelled -Value $null
        }
        $value = $key.KeyChar.ToString()
        Write-Host $value
        if ($AllowedKeys -ccontains $value -or $AllowedKeys -contains $value)
        {
            return New-GameWipChoiceResult -Status Selected -Value $value
        }
        Write-GameWipHost 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Read-GameWipIndexedChoiceResult
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices,
        [string]$Default
    )
    Assert-GameWipInteractiveConsole -Purpose $Prompt
    if ($Choices.Count -eq 0)
    {
        return New-GameWipChoiceResult -Status Cancelled -Value $null
    }
    Write-Host ''
    Write-Host $Prompt
    for ($index = 0; $index -lt $Choices.Count; ++$index)
    {
        Write-Host ("  [{0}] {1}" -f ($index + 1), $Choices[$index])
    }
    while ($true)
    {
        $inputPrompt = if ([string]::IsNullOrWhiteSpace($Default))
        {
            'Selection [Q = cancel]'
        }
        else
        {
            "Selection [Enter = $Default, Q = cancel]"
        }
        $answer = Read-Host $inputPrompt
        if ($answer -eq 'q' -or $answer -eq 'Q')
        {
            return New-GameWipChoiceResult -Status Cancelled -Value $null
        }
        if ([string]::IsNullOrWhiteSpace($answer) -and -not [string]::IsNullOrWhiteSpace($Default))
        {
            return New-GameWipChoiceResult -Status Selected -Value $Default
        }
        $number = 0
        if ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and $number -le $Choices.Count)
        {
            return New-GameWipChoiceResult -Status Selected -Value $Choices[($number - 1)]
        }
        Write-GameWipHost 'Enter one of the listed numbers or Q.' -ForegroundColor Yellow
    }
}

function Read-GameWipIndexedChoice
{
    param([string]$Prompt, [string[]]$Choices)
    $result = Read-GameWipIndexedChoiceResult -Prompt $Prompt -Choices $Choices
    if ($result.Status -eq 'Cancelled')
    {
        return $null
    }
    return $result.Value
}

function Read-GameWipMenuChoiceResult
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices,
        [string]$Default
    )
    Assert-GameWipInteractiveConsole -Purpose $Prompt
    $keys = @('1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'a', 'b', 'c', 'd', 'e', 'f')
    if ($Choices.Count -gt $keys.Count)
    {
        return Read-GameWipIndexedChoiceResult -Prompt $Prompt -Choices $Choices -Default $Default
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
        $key = Read-GameWipConsoleKey -Purpose $Prompt
        if ($key.Key -eq [ConsoleKey]::Escape -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q')
        {
            Write-Host 'cancel'
            return New-GameWipChoiceResult -Status Cancelled -Value $null
        }
        if ($key.Key -eq [ConsoleKey]::Enter -and -not [string]::IsNullOrWhiteSpace($Default))
        {
            Write-Host 'Enter'
            return New-GameWipChoiceResult -Status Selected -Value $Default
        }
        $selectionKey = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $selectionKey
        $index = [array]::IndexOf($keys, $selectionKey)
        if ($index -ge 0 -and $index -lt $Choices.Count)
        {
            return New-GameWipChoiceResult -Status Selected -Value $Choices[$index]
        }
        Write-GameWipHost 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Read-GameWipTextValue
{
    param([Parameter(Mandatory = $true)][string]$Prompt, [string]$Default = '')
    Assert-GameWipInteractiveConsole -Purpose $Prompt
    $value = if ([string]::IsNullOrWhiteSpace($Default))
    {
        Read-Host $Prompt
    }
    else
    {
        Read-Host "$Prompt [$Default]"
    }
    if ([string]::IsNullOrWhiteSpace($value))
    {
        return $Default
    }
    return $value
}

function Read-GameWipIntegerValue
{
    param([Parameter(Mandatory = $true)][string]$Prompt, [Parameter(Mandatory = $true)][int]$Default)
    Assert-GameWipInteractiveConsole -Purpose $Prompt
    while ($true)
    {
        $value = Read-GameWipTextValue -Prompt $Prompt -Default ([string]$Default)
        $parsed = 0
        if ([int]::TryParse($value, [ref]$parsed) -and $parsed -gt 0)
        {
            return $parsed
        }
        Write-GameWipHost 'Enter a positive integer.' -ForegroundColor Yellow
    }
}

function Read-GameWipYesNo
{
    param([Parameter(Mandatory = $true)][string]$Prompt, [Parameter(Mandatory = $true)][bool]$Default)
    Assert-GameWipInteractiveConsole -Purpose $Prompt
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
        $key = Read-GameWipConsoleKey -Purpose $Prompt
        if ($key.Key -eq [ConsoleKey]::Escape)
        {
            Write-Host 'cancel'
            throw [System.OperationCanceledException]::new("$Prompt was cancelled.")
        }
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            return $Default
        }
        $value = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $value
        if ($value -eq 'y')
        {
            return $true
        }
        if ($value -eq 'n')
        {
            return $false
        }
        Write-GameWipHost 'Enter y or n, or ESC to cancel.' -ForegroundColor Yellow
    }
}

function Read-GameWipMultiChoiceResult
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices
    )
    Assert-GameWipInteractiveConsole -Purpose $Prompt
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
        $key = Read-GameWipConsoleKey -Purpose $Prompt
        if ($key.Key -eq [ConsoleKey]::Escape -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q')
        {
            Write-Host 'cancel'
            return New-GameWipChoiceResult -Status Cancelled -Value $null
        }
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            return New-GameWipChoiceResult -Status Selected -Value @($Choices | Where-Object { $selected.Contains($_) })
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
        Write-GameWipHost 'Invalid selection.' -ForegroundColor Yellow
    }
}
