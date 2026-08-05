@{
    MsysRoot = 'C:\MSYS2'
    CMakeVersionPattern = '^cmake version 4\.4\.(?:[2-9]|[1-9][0-9]+)(?:\.|$)'

    WingetPackages = @(
        @{ Name = 'Git'; Id = 'Git.Git'; Command = 'git' }
    )
}
