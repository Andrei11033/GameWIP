# Shared configuration and repository-path helpers.
# Bootstrap validation is intentionally narrower than JSON Schema validation.

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Configuration loading
# ------------------------------------------------------------

function Resolve-GameWipRepositoryPath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([IO.Path]::IsPathRooted($Path))
    {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $Path))
}

function ConvertTo-GameWipHashtable
{
    param([AllowNull()]$Value)

    if ($null -eq $Value -or $Value -is [string] -or $Value.GetType().IsValueType)
    {
        return $Value
    }

    if ($Value -is [System.Collections.IDictionary])
    {
        $result = @{}

        foreach ($key in $Value.Keys)
        {
            $result[[string]$key] = ConvertTo-GameWipHashtable -Value $Value[$key]
        }

        return $result
    }

    if ($Value -is [System.Collections.IEnumerable])
    {
        $items = @($Value | ForEach-Object { ConvertTo-GameWipHashtable -Value $_ })
        return , $items
    }

    $result = @{}

    foreach ($property in $Value.PSObject.Properties)
    {
        $result[$property.Name] = ConvertTo-GameWipHashtable -Value $property.Value
    }

    return $result
}

function Read-GameWipJsonConfig
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$SchemaPath
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        throw "Required $Name configuration is missing: $Path"
    }

    try
    {
        $parsed = Read-GameWipUtf8Text -Path $Path | ConvertFrom-Json
    }

    catch
    {
        throw "Malformed $Name configuration '$Path': $($_.Exception.Message)"
    }

    $config = ConvertTo-GameWipHashtable -Value $parsed

    if (-not $config.Contains('schemaVersion'))
    {
        throw "$Name configuration '$Path' does not declare schemaVersion."
    }

    if ([int]$config.schemaVersion -ne 1)
    {
        throw "Unsupported $Name configuration schemaVersion '$($config.schemaVersion)'; expected 1."
    }

    # Bootstrap validation deliberately does not implement JSON Schema. The
    # conforming Draft 2020-12 validator is .github/scripts/validate_config_schemas.py.
    if (-not [string]::IsNullOrWhiteSpace($SchemaPath))
    {
        if (-not (Test-Path -LiteralPath $SchemaPath -PathType Leaf))
        {
            throw "Required $Name schema is missing: $SchemaPath"
        }

        try
        {
            $schemaHeader = Read-GameWipUtf8Text -Path $SchemaPath | ConvertFrom-Json
        }

        catch
        {
            throw "Malformed $Name schema '$SchemaPath': $($_.Exception.Message)"
        }

        if ([string]$schemaHeader.'$schema' -ne 'https://json-schema.org/draft/2020-12/schema')
        {
            throw "$Name schema '$SchemaPath' must declare JSON Schema draft 2020-12."
        }
    }

    return $config
}

# ------------------------------------------------------------
# Configuration validation
# ------------------------------------------------------------

function Assert-GameWipProjectConfig
{
    foreach ($field in @('repository', 'defaultBranch', 'storage', 'managedEnvironment'))
    {
        if (-not $ProjectConfig.Contains($field))
        {
            throw "Project configuration is missing '$field'."
        }
    }

    $storageRoot = Get-GameWipStorageRoot
    # Match paths using the host filesystem's case rules.
    $pathComparer = Get-GameWipPathComparer
    $pathComparison = Get-GameWipPathComparison
    $storagePaths = [System.Collections.Generic.HashSet[string]]::new($pathComparer)

    # Keep each storage area distinct beneath the configured root.
    foreach ($area in @('cache', 'state', 'temp', 'runs'))
    {
        $relativePath = [string]$ProjectConfig.storage[$area]

        if ([string]::IsNullOrWhiteSpace($relativePath) -or [IO.Path]::IsPathRooted($relativePath))
        {
            throw "Project storage '$area' must be a non-empty relative path."
        }

        $resolvedPath = Resolve-GameWipStoragePath -RelativePath $relativePath

        if ($resolvedPath.Equals($storageRoot, $pathComparison))
        {
            throw "Project storage '$area' must be a child of the storage root."
        }

        if (-not $storagePaths.Add($resolvedPath))
        {
            throw "Project storage paths must be distinct; '$area' duplicates '$resolvedPath'."
        }
    }

    # Keep managed tool roots beneath the helper storage root.
    foreach ($name in @('msys2Root', 'gameWipToolsRoot'))
    {
        $relativePath = [string]$ProjectConfig.managedEnvironment[$name]

        if ([string]::IsNullOrWhiteSpace($relativePath) -or [IO.Path]::IsPathRooted($relativePath))
        {
            throw "Managed environment path '$name' must be a non-empty path relative to the storage root."
        }

        $resolvedPath = Resolve-GameWipStoragePath -RelativePath $relativePath

        if ($resolvedPath.Equals($storageRoot, $pathComparison))
        {
            throw "Managed environment path '$name' must be a child of the storage root."
        }
    }

    $msys2Root = Get-GameWipMsys2Root
    $managedToolRoot = Get-GameWipManagedToolRoot
    $managedToolsParent = [IO.Path]::GetDirectoryName((Get-GameWipCanonicalPath -Path $managedToolRoot))
    $managedToolsName = [IO.Path]::GetFileName((Get-GameWipCanonicalPath -Path $managedToolRoot))
    $isExistingMsys2Layout = (
        $managedToolsParent.Equals((Get-GameWipCanonicalPath -Path $msys2Root), $pathComparison) -and
        $managedToolsName.Equals('GameWIPTools', $pathComparison)
    )
    $msys2ContainsManagedTools = Test-GameWipPathWithinRoot -Path $managedToolRoot -Root $msys2Root
    $managedToolsContainMsys2 = Test-GameWipPathWithinRoot -Path $msys2Root -Root $managedToolRoot

    # Allow only the documented GameWIPTools child inside an MSYS2 root.
    if (
        $msys2Root.Equals($storageRoot, $pathComparison) -or
        $managedToolRoot.Equals($storageRoot, $pathComparison) -or
        $managedToolsContainMsys2 -or
        ($msys2ContainsManagedTools -and -not $isExistingMsys2Layout)
    )
    {
        throw 'The MSYS2 and managed-tools roots must be separate; only GameWIPTools directly inside MSYS2 is supported.'
    }
}
