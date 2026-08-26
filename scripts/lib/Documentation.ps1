# GameWIP Documentation helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Invoke-GameWipMarkdownLink
{
    $checker = Join-Path $RepositoryRoot '.github\scripts\check_markdown_links.py'
    if (-not (Test-Path -LiteralPath $checker))
    {
        throw "Markdown-link checker is missing: $checker"
    }

    $python = Resolve-GameWipPython
    Write-GameWipSection 'Markdown links'
    Write-Host "  Python: $($python.Version) via $($python.Source)"
    Invoke-GameWipNative `
        -Name 'markdown-links' `
        -FilePath $python.Path `
        -Arguments @($checker, '--root', $RepositoryRoot)
}
