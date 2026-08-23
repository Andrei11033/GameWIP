@{
    DefaultConfigurePreset = 'test'
    DefaultBuildPreset = 'test'
    DefaultTestPreset = 'test'
    DefaultModule = 'all'
    DefaultStressCount = 32
    DefaultStressParallel = 8
    WorkspaceTemp = 'build\gamewip-temp'
    RunLogRoot = 'build\tool-runs'
    GitHubRepository = 'Andrei11033/GameWIP'
    GitHubDefaultBranch = 'master'

    Formatting = @{
        ClangFormatPath = 'C:\MSYS2\ucrt64\bin\clang-format.exe'
        ConfigPath = '.clang-format'
        SourceRoots = @(
            'foundation'
            'tools'
            'engine'
            'game'
        )
    }

    Unicode = @{
        Version = '17.0.0'
        CacheRoot = 'build\unicode-data'
        PythonPath = 'C:\MSYS2\ucrt64\bin\python.exe'
        Generator = 'foundation\unicode\tools\generate_unicode_data.py'
        GeneratedHeader = 'foundation\unicode\internal\generated\unicode_properties.h'
        UcdUrlTemplate = 'https://www.unicode.org/Public/{0}/ucd/UCD.zip'
        RequiredFiles = @(
            'auxiliary\GraphemeBreakProperty.txt'
            'DerivedCoreProperties.txt'
            'emoji\emoji-data.txt'
            'auxiliary\GraphemeBreakTest.txt'
        )
    }

    BenchmarkProfiles = @(
        @{
            Id = 'quick'
            Name = 'Quick development measurement'
            Repetitions = 1
            MinTime = '0.05s'
            AggregatesOnly = $false
        }
        @{
            Id = 'standard'
            Name = 'Repeatable local measurement'
            Repetitions = 5
            MinTime = '0.2s'
            AggregatesOnly = $true
        }
        @{
            Id = 'stable'
            Name = 'Longer comparison-quality measurement'
            Repetitions = 10
            MinTime = '1s'
            AggregatesOnly = $true
            RandomInterleaving = $true
        }
    )

    ManualWorkflows = @(
        @{
            Id = 'validation'
            Name = 'Run repository validation'
            File = 'validation.yml'
            Safety = 'check'
        }
        @{
            Id = 'project-dry-run'
            Name = 'Preview project reconciliation'
            File = 'project-automation.yml'
            Safety = 'dry-run'
        }
        @{
            Id = 'project-write'
            Name = 'Apply project reconciliation'
            File = 'project-automation.yml'
            Safety = 'write'
        }
        @{
            Id = 'release-check'
            Name = 'Check release readiness'
            File = 'release-preparation.yml'
            Safety = 'dry-run'
        }
        @{
            Id = 'release-prepare'
            Name = 'Create or update the release-preparation pull request'
            File = 'release-preparation.yml'
            Safety = 'write'
        }
        @{
            Id = 'release-finalize-dry-run'
            Name = 'Preview release finalization'
            File = 'release-preparation.yml'
            Safety = 'dry-run'
        }
        @{
            Id = 'release-finalize'
            Name = 'Create the immutable tag and GitHub release'
            File = 'release-preparation.yml'
            Safety = 'finalize'
        }
        @{
            Id = 'docs-deploy'
            Name = 'Build and deploy Doxygen GitHub Pages'
            File = 'docs.yml'
            Safety = 'deploy'
        }
    )

    Modules = @(
        'assert'
        'base'
        'filesystem'
        'io'
        'logger'
        'runner'
        'terminal'
        'test_support'
        'unicode'
        'window'
    )

    ProjectCommands = @(
        @{
            Id = 'test-all'
            Name = 'Run all correctness modules'
            BuildPreset = 'test'
            Executable = 'build\test\GameWIPTests.exe'
            Arguments = @('--no-test-report')
            UseWorkspaceTemp = $true
            AcceptsExtraArgs = $true
        }
        @{
            Id = 'benchmark-dry-run'
            Name = 'Run benchmark registration dry run'
            BuildPreset = 'benchmark'
            Executable = 'build\benchmark\GameWIPBenchmarks.exe'
            Arguments = @('--benchmark_dry_run')
            UseWorkspaceTemp = $true
            AcceptsExtraArgs = $true
        }
        @{
            Id = 'dev-version'
            Name = 'Print development executable version'
            BuildPreset = 'dev'
            Executable = 'build\dev\GameWIP.exe'
            Arguments = @('--version')
            UseWorkspaceTemp = $false
            AcceptsExtraArgs = $false
        }
        @{
            Id = 'release-version'
            Name = 'Print release executable version'
            BuildPreset = 'release'
            Executable = 'build\release\game\GameWIP.exe'
            AlternateExecutable = 'build\release\GameWIP.exe'
            Arguments = @('--version')
            UseWorkspaceTemp = $false
            AcceptsExtraArgs = $false
        }
    )

    Bundles = @(
        @{
            Id = 'quick'
            Name = 'Quick correctness validation'
            Steps = @(
                @{ Kind = 'Configure'; Preset = 'test' }
                @{ Kind = 'Build'; Preset = 'test' }
                @{ Kind = 'CTest'; Preset = 'test'; UseWorkspaceTemp = $true }
            )
        }
        @{
            Id = 'local-release-check'
            Name = 'Local release-readiness check'
            Steps = @(
                @{ Kind = 'Configure'; Preset = 'test' }
                @{ Kind = 'Build'; Preset = 'test' }
                @{ Kind = 'CTest'; Preset = 'test'; UseWorkspaceTemp = $true }
                @{ Kind = 'Configure'; Preset = 'coverage' }
                @{ Kind = 'Build'; Preset = 'coverage' }
                @{ Kind = 'CTest'; Preset = 'coverage'; UseWorkspaceTemp = $true }
                @{ Kind = 'BuildTarget'; Preset = 'coverage'; Target = 'coverage' }
                @{ Kind = 'Configure'; Preset = 'analyze' }
                @{ Kind = 'Build'; Preset = 'analyze' }
                @{ Kind = 'Configure'; Preset = 'docs' }
                @{ Kind = 'Build'; Preset = 'docs' }
                @{ Kind = 'ProjectCommand'; Command = 'benchmark-dry-run'; BuildIfMissing = $true }
                @{ Kind = 'ProjectCommand'; Command = 'release-version'; BuildIfMissing = $true }
            )
        }
        @{
            Id = 'sanitizer'
            Name = 'CLANG64 AddressSanitizer validation'
            Steps = @(
                @{ Kind = 'Configure'; Preset = 'asan' }
                @{ Kind = 'Build'; Preset = 'asan' }
                @{ Kind = 'CTest'; Preset = 'asan'; UseWorkspaceTemp = $true }
            )
        }
    )
}
