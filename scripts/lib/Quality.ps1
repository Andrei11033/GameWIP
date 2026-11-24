# Quality workflow entry point.

# ------------------------------------------------------------
# Quality workflow
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Invoke-GameWipQuality
{
    param(
        [ValidateSet('check', 'fix')][string]$Mode = 'check',
        [switch]$FailFast,
        [switch]$Changed
    )
    Initialize-GameWipStorage
    $scopeInfo = Get-GameWipQualityScope -Changed:$Changed
    $qualityTools = $null

    if ($Mode -eq 'fix')
    {
        $qualityTools = Get-GameWipQualityToolchain
        Invoke-GameWipQualityFix -ScopeInfo $scopeInfo -Tools $qualityTools
    }
    Invoke-GameWipQualityCheck -FailFast:$FailFast -Changed:$Changed -ScopeInfo $scopeInfo -Tools $qualityTools
}
