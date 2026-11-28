# Console input and output primitives. Command dispatch stays with the caller.

# ------------------------------------------------------------
# Output rendering and input primitives
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipSemanticColor
{
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('Success', 'Failure', 'Warning', 'Accent', 'Muted')]
        [string]$Semantic
    )

    switch ($Semantic)
    {
        'Success'
        {
            return [ConsoleColor]::Green
        }

        'Failure'
        {
            return [ConsoleColor]::Red
        }

        'Warning'
        {
            return [ConsoleColor]::Yellow
        }

        'Accent'
        {
            return [ConsoleColor]::Cyan
        }

        'Muted'
        {
            return [ConsoleColor]::DarkGray
        }
    }
}

function Write-GameWipSemanticText
{
    param(
        [AllowEmptyString()][string]$Object = '',
        [Parameter(Mandatory = $true)]
        [ValidateSet('Success', 'Failure', 'Warning', 'Accent', 'Muted')]
        [string]$Semantic,
        [switch]$NoNewline
    )

    $color = Get-GameWipSemanticColor -Semantic $Semantic
    Write-GameWipHost -Object $Object -ForegroundColor $color -NoNewline:$NoNewline
}

function Write-GameWipStatusLine
{
    param(
        [Parameter(Mandatory = $true)][string]$Status,
        [AllowEmptyString()][string]$Text = '',
        [AllowEmptyString()][string]$Suffix = '',
        [Parameter(Mandatory = $true)]
        [ValidateSet('Success', 'Failure', 'Warning', 'Accent', 'Muted')]
        [string]$Semantic,
        [ValidateSet('Success', 'Failure', 'Warning', 'Accent', 'Muted')]
        [string]$SuffixSemantic,
        [ValidateRange(0, 32)][int]$Indent = 0,
        [ValidateRange(0, 80)][int]$MarkerWidth = 0
    )

    $marker = "[$Status]"

    if ($MarkerWidth -gt $marker.Length)
    {
        $marker = $marker.PadRight($MarkerWidth)
    }

    if ($Indent -gt 0)
    {
        Write-Host (' ' * $Indent) -NoNewline
    }

    Write-GameWipSemanticText -Object $marker -Semantic $Semantic -NoNewline

    if ([string]::IsNullOrEmpty($Text) -and [string]::IsNullOrEmpty($Suffix))
    {
        Write-Host ''
        return
    }

    if (-not [string]::IsNullOrEmpty($Text))
    {
        if ([string]::IsNullOrEmpty($Suffix))
        {
            Write-Host " $Text"
            return
        }

        Write-Host " $Text " -NoNewline
    }
    else
    {
        Write-Host ' ' -NoNewline
    }

    if ($PSBoundParameters.ContainsKey('SuffixSemantic'))
    {
        Write-GameWipSemanticText -Object $Suffix -Semantic $SuffixSemantic
        return
    }

    Write-Host $Suffix
}

function Write-GameWipSection
{
    param([Parameter(Mandatory = $true)][string]$Title)

    Write-Host ''
    Write-GameWipSemanticText -Object $Title -Semantic Accent
    Write-GameWipSemanticText -Object ('=' * $Title.Length) -Semantic Accent
}

# ------------------------------------------------------------
# Action menu input
# ------------------------------------------------------------

function Read-GameWipActionMenuItem
{
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][object[]]$Items,
        [Parameter(Mandatory = $true)][ValidateSet('Back', 'Exit')][string]$ExitLabel,
        [scriptblock]$Header
    )

    Write-GameWipSection $Title

    if ($null -ne $Header)
    {
        & $Header
    }

    foreach ($item in $Items)
    {
        Write-Host "$($item.Key). $($item.Label)"
    }
    Write-Host "Q. $ExitLabel"

    $keys = @($Items | ForEach-Object { [string]$_.Key })
    $choice = Read-GameWipActionChoice -Prompt $Prompt -AllowedKeys $keys -AllowCancel

    if ($choice.Status -eq 'Cancelled')
    {
        return New-GameWipChoiceResult -Status Cancelled -Value $null
    }

    $selected = $Items | Where-Object { [string]$_.Key -ieq [string]$choice.Value } | Select-Object -First 1
    return New-GameWipChoiceResult -Status Selected -Value ([string]$selected.Handler)
}

function Test-GameWipInteractiveConsole
{
    try
    {
        return (
            -not [Console]::IsInputRedirected -and
            -not [Console]::IsOutputRedirected -and
            $Host.Name -ne 'ServerRemoteHost'
        )
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
                -SuggestedActions @(
                'Use the corresponding explicit CLI command instead of an interactive menu.',
                'Do not pipe or redirect stdin for interactive helper flows.'
            ))
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

function Read-GameWipActionChoice
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$AllowedKeys,
        [switch]$AllowCancel
    )
    Assert-GameWipInteractiveConsole -Purpose $Prompt

    while ($true)
    {
        $inputPrompt = if ($AllowCancel)
        {
            "$Prompt [press Enter; Q = cancel]"
        }
        else
        {
            "$Prompt [press Enter]"
        }
        $value = [string](Read-Host $inputPrompt)
        $value = $value.Trim()

        if ($AllowCancel -and $value -ieq 'q')
        {
            return New-GameWipChoiceResult -Status Cancelled -Value $null
        }

        $selectedKey = $AllowedKeys | Where-Object { [string]$_ -ieq $value } | Select-Object -First 1

        if ($null -ne $selectedKey)
        {
            return New-GameWipChoiceResult -Status Selected -Value ([string]$selectedKey)
        }

        Write-GameWipHost 'Invalid selection.' -ForegroundColor Yellow
    }
}

# ------------------------------------------------------------
# Indexed and keyed choices
# ------------------------------------------------------------

function Read-GameWipIndexedChoiceResult
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices,
        [string]$Default,
        [switch]$AllowMultiple
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
            'Type a listed number, then press Enter; Q = cancel'
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

        if ($AllowMultiple)
        {
            $parts = @($answer -split '[,\s]+' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
            $numbers = [System.Collections.Generic.List[int]]::new()
            $valid = $parts.Count -gt 0

            foreach ($part in $parts)
            {
                $number = 0

                if (
                    -not [int]::TryParse($part, [ref]$number) -or
                    $number -lt 1 -or
                    $number -gt $Choices.Count -or
                    $numbers.Contains($number)
                )
                {
                    $valid = $false
                    break
                }

                $numbers.Add($number)
            }

            if ($valid)
            {
                return New-GameWipChoiceResult -Status Selected -Value @($numbers | ForEach-Object { $Choices[$_ - 1] })
            }
        }

        # Accept one listed choice when the answer is not a valid multi-selection.
        $number = 0

        if ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and $number -le $Choices.Count)
        {
            return New-GameWipChoiceResult -Status Selected -Value $Choices[($number - 1)]
        }

        $message = if ($AllowMultiple)
        {
            'Enter one or more listed numbers separated by commas, or Q.'
        }
        else
        {
            'Enter one of the listed numbers or Q.'
        }
        Write-GameWipHost $message -ForegroundColor Yellow
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
        $inputPrompt = if ([string]::IsNullOrWhiteSpace($Default))
        {
            'Type a listed key, then press Enter; Q = cancel'
        }
        else
        {
            "Selection [Enter = $Default, Q = cancel]"
        }
        $answer = [string](Read-Host $inputPrompt)
        $answer = $answer.Trim()

        if ($answer -ieq 'q')
        {
            return New-GameWipChoiceResult -Status Cancelled -Value $null
        }

        if ([string]::IsNullOrWhiteSpace($answer) -and -not [string]::IsNullOrWhiteSpace($Default))
        {
            return New-GameWipChoiceResult -Status Selected -Value $Default
        }

        $selectionKey = $answer.ToLowerInvariant()
        $index = [array]::IndexOf($keys, $selectionKey)

        if ($index -ge 0 -and $index -lt $Choices.Count)
        {
            return New-GameWipChoiceResult -Status Selected -Value $Choices[$index]
        }

        Write-GameWipHost 'Enter one listed key, then press Enter, or Q to cancel.' -ForegroundColor Yellow
    }
}

# ------------------------------------------------------------
# Typed values
# ------------------------------------------------------------

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
        '[Y/n, Enter = yes, Q = cancel]'
    }
    else
    {
        '[y/N, Enter = no, Q = cancel]'
    }

    while ($true)
    {
        $value = [string](Read-Host "$Prompt $suffix")
        $value = $value.Trim()

        if ($value -ieq 'q')
        {
            throw [System.OperationCanceledException]::new("$Prompt was cancelled.")
        }

        if ([string]::IsNullOrWhiteSpace($value))
        {
            return $Default
        }

        $value = $value.ToLowerInvariant()

        if ($value -eq 'y')
        {
            return $true
        }

        if ($value -eq 'n')
        {
            return $false
        }

        Write-GameWipHost 'Enter y or n, then press Enter, or Q to cancel.' -ForegroundColor Yellow
    }
}

# ------------------------------------------------------------
# Multiple choices
# ------------------------------------------------------------

function Read-GameWipMultiChoiceResult
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices
    )
    return Read-GameWipIndexedMultiChoiceResult -Prompt $Prompt -Choices $Choices
}

function ConvertTo-GameWipMultiChoiceIndexes
{
    param([Parameter(Mandatory = $true)][string]$InputText, [Parameter(Mandatory = $true)][int]$ChoiceCount)

    if ([string]::IsNullOrWhiteSpace($InputText))
    {
        return @()
    }

    $indexes = [System.Collections.Generic.HashSet[int]]::new()

    foreach ($token in @($InputText -split ','))
    {
        $trimmed = $token.Trim()
        $number = 0

        if (
            [string]::IsNullOrWhiteSpace($trimmed) -or
            -not [int]::TryParse($trimmed, [ref]$number) -or
            $number -lt 1 -or
            $number -gt $ChoiceCount
        )
        {
            throw "Enter comma-separated numbers from 1 to $ChoiceCount."
        }

        [void]$indexes.Add($number)
    }

    return @($indexes | Sort-Object)
}

function Read-GameWipIndexedMultiChoiceResult
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices
    )
    Assert-GameWipInteractiveConsole -Purpose $Prompt

    if ($Choices.Count -eq 0)
    {
        return New-GameWipChoiceResult -Status Cancelled -Value $null
    }

    $selected = [System.Collections.Generic.HashSet[int]]::new()

    while ($true)
    {
        Write-Host ''
        Write-Host $Prompt

        for ($index = 0; $index -lt $Choices.Count; ++$index)
        {
            $marker = if ($selected.Contains($index + 1))
            {
                'x'
            }
            else
            {
                ' '
            }
            Write-Host ("  [{0}] [{1}] {2}" -f ($index + 1), $marker, $Choices[$index])
        }
        $answer = Read-Host 'Toggle numbers separated by commas, Enter to accept, or Q to cancel'

        if ($answer -eq 'q' -or $answer -eq 'Q')
        {
            return New-GameWipChoiceResult -Status Cancelled -Value $null
        }

        if ([string]::IsNullOrWhiteSpace($answer))
        {
            $values = [System.Collections.Generic.List[string]]::new()

            for ($index = 0; $index -lt $Choices.Count; ++$index)
            {
                if ($selected.Contains($index + 1))
                {
                    $values.Add($Choices[$index]) | Out-Null
                }
            }

            return New-GameWipChoiceResult -Status Selected -Value $values.ToArray()
        }

        try
        {
            $indexes = @(ConvertTo-GameWipMultiChoiceIndexes -InputText $answer -ChoiceCount $Choices.Count)

            foreach ($number in $indexes)
            {
                if ($selected.Contains($number))
                {
                    [void]$selected.Remove($number)
                }
                else
                {
                    [void]$selected.Add($number)
                }
            }
        }

        catch
        {
            Write-GameWipHost $_.Exception.Message -ForegroundColor Yellow
        }
    }
}
