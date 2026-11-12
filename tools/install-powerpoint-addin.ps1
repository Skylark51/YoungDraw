param([string]$YoungDraw = (Join-Path $PSScriptRoot '../YoungDraw.exe'))

$ErrorActionPreference = 'Stop'
$executable = (Resolve-Path -LiteralPath $YoungDraw).ProviderPath
$source = Join-Path $PSScriptRoot 'YoungDrawPowerPointAddIn.cs'
$framework = Join-Path $env:WINDIR 'Microsoft.NET/Framework/v4.0.30319'
$compiler = Join-Path $framework 'csc.exe'
$windowsBase = Join-Path $framework 'WPF/WindowsBase.dll'
$hash = (Get-FileHash -LiteralPath $source).Hash.Substring(0, 16)
$installFolder = Join-Path $env:LOCALAPPDATA "YoungDraw/PowerPoint/$hash"
$library = Join-Path $installFolder 'YoungDrawPowerPointAddIn.dll'

if (!(Test-Path -LiteralPath $library -PathType Leaf)) {
    New-Item -Path $installFolder -ItemType Directory -Force | Out-Null
    $compilerArgs = @(
        '/nologo', '/target:library', '/optimize+', '/codepage:65001', "/out:$library",
        "/reference:$windowsBase", '/reference:System.Windows.Forms.dll',
        '/reference:System.Xml.Linq.dll', '/reference:Microsoft.CSharp.dll', $source
    )
    & $compiler @compilerArgs
    if ($LASTEXITCODE) { throw 'Could not build the YoungDraw PowerPoint add-in.' }
}

$progId = 'YoungDraw.PowerPointAddIn'
$classId = '{D2483040-86AF-4FC1-A6EC-B808483787EB}'
$assembly = [Reflection.AssemblyName]::GetAssemblyName($library)
$codeBase = ([Uri]::new($library)).GetComponents([UriComponents]::AbsoluteUri, [UriFormat]::SafeUnescaped)

function Set-AddInKey($root, [string]$path, [hashtable]$values) {
    $key = $root.CreateSubKey($path)
    try {
        foreach ($entry in $values.GetEnumerator()) {
            $key.SetValue([string]$entry.Key, $entry.Value)
        }
    } finally {
        $key.Dispose()
    }
}

$views = @([Microsoft.Win32.RegistryView]::Registry32)
if ([Environment]::Is64BitOperatingSystem) { $views += [Microsoft.Win32.RegistryView]::Registry64 }
foreach ($view in $views) {
    $user = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $view)
    try {
        $classes = 'Software\Classes'
        $class = "$classes\CLSID\$classId"
        Set-AddInKey $user "$classes\$progId" @{ '' = $progId }
        Set-AddInKey $user "$classes\$progId\CLSID" @{ '' = $classId }
        Set-AddInKey $user $class @{ '' = 'YoungDraw.PowerPoint.AddIn' }
        Set-AddInKey $user "$class\ProgID" @{ '' = $progId }
        Set-AddInKey $user "$class\Implemented Categories\{62C8FE65-4EBB-45E7-B440-6E39B2CDBF29}" @{}
        $managed = @{
            Class = 'YoungDraw.PowerPoint.AddIn'
            Assembly = $assembly.FullName
            RuntimeVersion = 'v4.0.30319'
            CodeBase = $codeBase
        }
        Set-AddInKey $user "$class\InprocServer32\$($assembly.Version)" $managed
        Set-AddInKey $user "$class\InprocServer32" ($managed + @{ '' = 'mscoree.dll'; ThreadingModel = 'Both' })
        Set-AddInKey $user "Software\Microsoft\Office\PowerPoint\Addins\$progId" @{
            FriendlyName = 'YoungDraw'
            Description = 'Open an embedded ChemDraw drawing in YoungDraw.'
            LoadBehavior = [int]3
            YoungDrawPath = $executable
        }
    } finally {
        $user.Dispose()
    }
}

Write-Output "Registered $progId for the current user."
