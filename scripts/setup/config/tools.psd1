@{
    MsysRoot = 'C:\MSYS2'
    CMakeVersionPattern = '^cmake version 4\.4\.'

    WingetPackages = @(
        @{ Name = 'Git'; Id = 'Git.Git'; Command = 'git' }
    )
}
