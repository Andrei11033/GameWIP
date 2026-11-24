# Shared option selection and argument composition for interactive command builders.

# ------------------------------------------------------------
# Builder option filtering and prompts
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipCommandBuilderOptions
{
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Options,
        [string[]]$CommonCapabilities = @(),
        [string]$Platform = ''
    )

    $applicable = [System.Collections.Generic.List[object]]::new()

    foreach ($option in $Options)
    {
        $requiredCapabilities = @(if ($option -is [hashtable] -and $option.ContainsKey('requiresCommon'))
            {
                @($option['requiresCommon'])
            }
            else
            {
                @()
            })
        $requiredPlatforms = @(if ($option -is [hashtable] -and $option.ContainsKey('requiresPlatforms'))
            {
                @($option['requiresPlatforms'])
            }
            else
            {
                @()
            })
        $isApplicable = $true

        foreach ($capability in $requiredCapabilities)
        {
            if ($CommonCapabilities -notcontains [string]$capability)
            {
                $isApplicable = $false
                break
            }
        }

        if ($isApplicable -and $requiredPlatforms.Count -gt 0 -and $requiredPlatforms -notcontains $Platform)
        {
            $isApplicable = $false
        }

        if ($isApplicable)
        {
            $applicable.Add($option) | Out-Null
        }
    }

    return $applicable.ToArray()
}

function Read-GameWipCommandBuilderOptionResult
{
    param([Parameter(Mandatory = $true)]$Option)

    Write-GameWipSemanticText -Object $Option.Description -Semantic Muted

    switch ([string]$Option.Kind)
    {
        'boolean'
        {
            return Read-GameWipYesNo -Prompt $Option.Title -Default ([bool]$Option.Default)
        }

        'choice'
        {
            $choiceResult = Read-GameWipMenuChoiceResult `
                -Prompt $Option.Title `
                -Choices ([string[]]$Option.Choices) `
                -Default ([string]$Option.Default)

            if ($choiceResult.Status -eq 'Cancelled')
            {
                return New-GameWipChoiceResult -Status Cancelled -Value $null
            }
            return $choiceResult.Value
        }

        'integer'
        {
            return Read-GameWipIntegerValue -Prompt $Option.Title -Default ([int]$Option.Default)
        }

        'text'
        {
            return Read-GameWipTextValue -Prompt $Option.Title -Default ([string]$Option.Default)
        }

        'multi-choice'
        {
            return Read-GameWipMultiChoiceResult -Prompt $Option.Title -Choices ([string[]]$Option.Choices)
        }

        default
        {
            throw "Unknown command builder option kind '$($Option.Kind)' for '$($Option.Id)'."
        }
    }
}

# ------------------------------------------------------------
# Builder argument composition
# ------------------------------------------------------------

function ConvertTo-GameWipCommandBuilderArguments
{
    param([Parameter(Mandatory = $true)]$Option, [Parameter(Mandatory = $true)][AllowEmptyCollection()]$Value)

    switch ([string]$Option.Kind)
    {
        'boolean'
        {
            return @($Option.Arguments | Where-Object { [bool]$Value } | ForEach-Object { [string]$_ })
        }

        'multi-choice'
        {
            $selected = @($Value | ForEach-Object { [string]$_ })

            if ($selected.Count -eq 0)
            {
                return @()
            }

            # Validate selections before combining them into one argument value.
            if (@($selected | Where-Object { @($Option.Choices) -notcontains $_ }).Count -gt 0)
            {
                throw "Command builder option '$($Option.Id)' contains an unknown choice."
            }

            $exclusive = if ($Option.ContainsKey('exclusiveChoices'))
            {
                @($Option.ExclusiveChoices)
            }
            else
            {
                @()
            }

            if (@($selected | Where-Object { $exclusive -contains $_ }).Count -gt 0 -and $selected.Count -gt 1)
            {
                throw "Command builder option '$($Option.Id)' combines an exclusive choice with other values."
            }

            $separator = [string]$Option.ValueSeparator
            $selectedValue = $selected -join $separator
            return @($Option.Arguments | ForEach-Object { ([string]$_).Replace('{value}', $selectedValue) })
        }

        default
        {
            if ($Option.ContainsKey('omitDefault') -and [bool]$Option.OmitDefault -and ([string]$Value -eq [string]$Option.Default))
            {
                return @()
            }

            return @($Option.Arguments | ForEach-Object { ([string]$_).Replace('{value}', [string]$Value) })
        }
    }
}
