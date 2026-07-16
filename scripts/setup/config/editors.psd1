@{
    Default = @('vscode')

    Options = @(
        @{
            Key = '1'
            Id = 'vscode'
            Name = 'Visual Studio Code'
            Recommended = $true
            Handler = 'vscode'
            Package = 'Microsoft.VisualStudioCode'
            Command = 'code'
            Extensions = @(
                'ms-vscode.cpptools'
                'ms-vscode.cmake-tools'
            )
        }
        @{
            Key = '2'
            Id = 'visual-studio'
            Name = 'Visual Studio Community'
            Recommended = $false
            Handler = 'visual-studio'
            Package = 'Microsoft.VisualStudio.Community'
        }
    )
}
