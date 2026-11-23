param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$ArtifactDirectory
)
$ErrorActionPreference = 'Stop'
$taskExecutable = (Resolve-Path -LiteralPath $Executable).Path
$taskArtifacts = [IO.Path]::GetFullPath($ArtifactDirectory)
if (Test-Path -LiteralPath $taskArtifacts) { throw 'Use a new artifact directory to exclude stale export outputs.' }
[IO.Directory]::CreateDirectory($taskArtifacts) | Out-Null
$taskPrefix = Join-Path $taskArtifacts 'portable'
& (Join-Path $PSScriptRoot 'test-isolated.ps1') -BuildDirectory $taskArtifacts -Executable $taskExecutable -Arguments @('--export-demo', $taskPrefix)
$taskExpected = @('.cdxml','.svg','-mechanism.ydraw','-mechanism.cdxml','-mechanism.svg','-coordinate.ydraw','-coordinate.cdxml','-coordinate.svg','-rigid.ydraw','-rigid.cdxml','-rigid.svg','-rigid.png','-coordinate.png','-mechanism.png','-delocalized.ydraw','-delocalized.cdxml','-delocalized.svg','-delocalized.png','-cycle.ydraw','-cycle.png')
foreach ($taskSuffix in $taskExpected) {
    $taskPath = $taskPrefix + $taskSuffix
    if (!(Test-Path -LiteralPath $taskPath -PathType Leaf) -or (Get-Item -LiteralPath $taskPath).Length -lt 8) { throw "Missing or empty export: $taskSuffix" }
    if ($taskSuffix.EndsWith('.png')) {
        $taskBytes = [IO.File]::ReadAllBytes($taskPath)
        if ([BitConverter]::ToString($taskBytes,0,8) -ne '89-50-4E-47-0D-0A-1A-0A') { throw "Invalid PNG: $taskSuffix" }
    } elseif ($taskSuffix.EndsWith('.svg') -or $taskSuffix.EndsWith('.cdxml')) {
        $taskXml = New-Object System.Xml.XmlDocument
        $taskXml.XmlResolver = $null
        $taskReaderSettings = New-Object System.Xml.XmlReaderSettings
        $taskReaderSettings.DtdProcessing = [System.Xml.DtdProcessing]::Ignore
        $taskReaderSettings.XmlResolver = $null
        $taskReader = [System.Xml.XmlReader]::Create($taskPath,$taskReaderSettings)
        try { $taskXml.Load($taskReader) } finally { $taskReader.Dispose() }
        $taskRootName = if ($taskSuffix.EndsWith('.svg')) { 'svg' } else { 'CDXML' }
        if ($taskXml.DocumentElement.LocalName -ne $taskRootName) { throw "Wrong XML root: $taskSuffix" }
    }
}
$taskOutputHashes = foreach ($taskSuffix in $taskExpected) { $taskPath=$taskPrefix+$taskSuffix; [ordered]@{ file=[IO.Path]::GetFileName($taskPath); bytes=(Get-Item -LiteralPath $taskPath).Length; sha256=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash } }
$taskReceipt = [ordered]@{ executable=$taskExecutable; sha256=(Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash; exported_files=$taskExpected.Count; outputs=@($taskOutputHashes); exit_code=0; mode='actual executable --export-demo on isolated desktop'; utc=[DateTime]::UtcNow.ToString('o') }
$taskReceipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskArtifacts 'smoke.json') -Encoding UTF8
Write-Host "PASS: actual executable exported and validated $($taskExpected.Count) files."
