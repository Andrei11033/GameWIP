@{
    Severity = @('Warning', 'Error')
    ExcludeRules = @(
        'PSAvoidUsingWriteHost'
        'PSUseBOMForUnicodeEncodedFile'
        # GameWIP helper functions are private implementation functions. Collection-returning
        # names and cleanup/constructor verbs are clearer here than public-cmdlet conventions.
        'PSUseShouldProcessForStateChangingFunctions'
        'PSUseSingularNouns'
        # Entrypoint parameters and bootstrap globals are consumed by dot-sourced helper files,
        # which PSScriptAnalyzer cannot resolve across script boundaries.
        'PSReviewUnusedParameter'
        'PSUseDeclaredVarsMoreThanAssignments'
        # The helper regression suite intentionally replaces Read-Host to exercise menus.
        'PSAvoidOverwritingBuiltInCmdlets'
    )
    Rules = @{
        PSPlaceOpenBrace = @{
            Enable = $true
            OnSameLine = $false
            NewLineAfter = $true
            IgnoreOneLineBlock = $false
        }
        PSPlaceCloseBrace = @{
            Enable = $true
            NewLineAfter = $true
            IgnoreOneLineBlock = $false
            NoEmptyLineBefore = $false
        }
        PSUseConsistentIndentation = @{
            Enable = $true
            Kind = 'space'
            IndentationSize = 4
            PipelineIndentation = 'IncreaseIndentationForFirstPipeline'
        }
        PSUseConsistentWhitespace = @{
            Enable = $true
            CheckInnerBrace = $true
            CheckOpenBrace = $true
            CheckOpenParen = $true
            CheckOperator = $true
            CheckPipe = $true
            CheckPipeForRedundantWhitespace = $false
            CheckSeparator = $true
            CheckParameter = $false
        }
    }
}
