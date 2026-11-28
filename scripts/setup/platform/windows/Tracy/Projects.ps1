# Tracy project builds and runtime staging

# ------------------------------------------------------------
# Configure, build, and stage each executable
# ------------------------------------------------------------

function Invoke-GameWipTracyProjectBuild
{
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$UcrtBin,
        [Parameter(Mandatory = $true)][string]$CMake,
        [Parameter(Mandatory = $true)][string]$TracyRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$CacheRoot,
        [Parameter(Mandatory = $true)][string]$CompilerCompatibilityHeader
    )

    $runtimeDependenciesVisited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    $projects = @(
        @{ Name = 'profiler'; Source = 'profiler'; Outputs = @('tracy-profiler.exe') }
        @{ Name = 'capture'; Source = 'capture'; Outputs = @('tracy-capture.exe') }
        @{ Name = 'csvexport'; Source = 'csvexport'; Outputs = @('tracy-csvexport.exe') }
        @{ Name = 'import'; Source = 'import'; Outputs = @('tracy-import-chrome.exe', 'tracy-import-fuchsia.exe') }
        @{ Name = 'update'; Source = 'update'; Outputs = @('tracy-update.exe') }
    )

    $previousCache = $env:CPM_SOURCE_CACHE
    $previousPath = $env:Path
    $previousGitConfigCount = $env:GIT_CONFIG_COUNT
    $previousGitConfigKey = $env:GIT_CONFIG_KEY_0
    $previousGitConfigValue = $env:GIT_CONFIG_VALUE_0

    try
    {
        # Set up the shared build environment.
        $env:CPM_SOURCE_CACHE = $cacheRoot
        $env:Path = @($ucrtBin, $previousPath) -join [IO.Path]::PathSeparator
        $env:GIT_CONFIG_COUNT = '1'
        $env:GIT_CONFIG_KEY_0 = 'safe.directory'
        $env:GIT_CONFIG_VALUE_0 = $tracyRoot.Replace('\', '/')
        $cmakeUcrtBin = $ucrtBin.Replace('\', '/')
        $cmakeCompilerCompatibilityHeader = $compilerCompatibilityHeader.Replace('\', '/')

        foreach ($project in $projects)
        {
            Write-Host "Building Tracy $($project.Name) $version from the locked dependency source..."
            $source = Join-Path $tracyRoot $project.Source
            $build = Join-Path $buildRoot $project.Name

            # Configure the project with the selected UCRT64 toolchain.
            if (Test-Path -LiteralPath $build)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $build -OwnedRoot $buildRoot
            }
            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList @(
                '-S', $source, '-B', $build,
                '-G', 'Ninja',
                '-DCMAKE_BUILD_TYPE=Release',
                "-DCMAKE_C_COMPILER=$cmakeUcrtBin/gcc.exe",
                "-DCMAKE_CXX_COMPILER=$cmakeUcrtBin/g++.exe",
                "-DCMAKE_RC_COMPILER=$cmakeUcrtBin/windres.exe",
                "-DCMAKE_CXX_FLAGS=-march=x86-64-v3 -include cstdint -include `"$cmakeCompilerCompatibilityHeader`"",
                '-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++',
                '-DCMAKE_CXX_STANDARD_LIBRARIES=-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lws2_32 -ldbghelp -lsecur32',
                '-DNO_ISA_EXTENSIONS=ON'
            ) | Out-Null

            # Tracy's Release LTO flags create incompatible COFF objects under
            # UCRT64 GCC. Strip them from generated files, not cached sources.
            foreach ($ninjaFile in Get-ChildItem -LiteralPath $build -Recurse -Filter '*.ninja')
            {
                $ninjaText = Get-Content -LiteralPath $ninjaFile.FullName -Raw
                $updatedNinjaText = $ninjaText.Replace('-flto=auto', '')
                $updatedNinjaText = $updatedNinjaText.Replace('-fno-fat-lto-objects', '')

                if ($updatedNinjaText -ne $ninjaText)
                {
                    [IO.File]::WriteAllText(
                        $ninjaFile.FullName,
                        $updatedNinjaText,
                        [Text.UTF8Encoding]::new($false)
                    )
                }
            }

            # Build the project and stage its declared outputs and dependencies.
            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList @(
                '--build', $build, '--parallel', '--clean-first'
            ) | Out-Null

            foreach ($output in $project.Outputs)
            {
                $built = Get-ChildItem -LiteralPath $build -Recurse -Filter $output |
                    Select-Object -First 1

                if (-not $built)
                {
                    throw "Tracy $($project.Name) build did not produce $output."
                }

                Copy-Item -LiteralPath $built.FullName -Destination (Join-Path $stageRoot $output) -Force
                Copy-GameWipTracyRuntimeDependency `
                    -Executable $built.FullName `
                    -UcrtBin $ucrtBin `
                    -StageRoot $stageRoot `
                    -Visited $runtimeDependenciesVisited
            }
        }
    }

    finally
    {
        $env:CPM_SOURCE_CACHE = $previousCache
        $env:Path = $previousPath
        $env:GIT_CONFIG_COUNT = $previousGitConfigCount
        $env:GIT_CONFIG_KEY_0 = $previousGitConfigKey
        $env:GIT_CONFIG_VALUE_0 = $previousGitConfigValue
    }

}
