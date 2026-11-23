param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [string]$Filter = '',
    [string]$Configuration = '',
    [string]$Executable = '',
    [string[]]$Arguments = @(),
    [string]$ArtifactDirectory = '',
    [ValidateSet('baseline','after')][string]$Stage = 'after'
)
$ErrorActionPreference = 'Stop'
# Never switch the user's desktop or fall back to showing test windows there.
if (-not ('YoungDraw.IsolatedTests' -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
namespace YoungDraw {
 public static class IsolatedTests {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct Startup {
   public int cb; public string reserved, desktop, title;
   public int x,y,xSize,ySize,xChars,yChars,fill,flags;
   public short show,reservedBytes; public IntPtr reservedPointer,input,output,error;
  }
  [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr process,thread; public int pid,tid; }
  [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateDesktopW(string name,IntPtr device,IntPtr mode,int flags,uint access,IntPtr security);
  [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateProcessW(string app,StringBuilder command,IntPtr processSecurity,IntPtr threadSecurity,bool inherit,uint flags,IntPtr environment,string directory,ref Startup startup,out ProcessInfo process);
  [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process,out int code);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  public static int Run(string exe,string command) {
   string name="YoungDrawTests-"+Guid.NewGuid().ToString("N");
   IntPtr desktop=CreateDesktopW(name,IntPtr.Zero,IntPtr.Zero,0,0x10000000,IntPtr.Zero);
   if(desktop==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot create isolated test desktop");
   ProcessInfo process=new ProcessInfo();
   try {
    Startup startup=new Startup();startup.cb=Marshal.SizeOf(typeof(Startup));startup.desktop=name;
    if(!CreateProcessW(exe,new StringBuilder(command),IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,null,ref startup,out process))throw new Win32Exception(Marshal.GetLastWin32Error());
    uint wait;do {wait=WaitForSingleObject(process.process,500);} while(wait==258);
    if(wait!=0)throw new Win32Exception(Marshal.GetLastWin32Error());
    int code;if(!GetExitCodeProcess(process.process,out code))throw new Win32Exception(Marshal.GetLastWin32Error());return code;
   } finally {if(process.thread!=IntPtr.Zero)CloseHandle(process.thread);if(process.process!=IntPtr.Zero)CloseHandle(process.process);CloseDesktop(desktop);}
  }
 }
}
"@
}
$taskDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$taskPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
function Quote-TestLiteral([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
if ($Executable) {
    if ($Filter) { throw 'Filter is only valid for CTest.' }
    $taskExecutable = (Resolve-Path -LiteralPath $Executable).Path
    $taskLog = Join-Path $taskDirectory ('isolated-' + [IO.Path]::GetFileNameWithoutExtension($taskExecutable) + '.log')
    $taskCommand = '& ' + (Quote-TestLiteral $taskExecutable)
    foreach ($taskArgument in $Arguments) { $taskCommand += ' ' + (Quote-TestLiteral $taskArgument) }
} else {
    if ($Arguments.Count) { throw 'Arguments requires Executable.' }
    $taskLog = Join-Path $taskDirectory 'isolated-ctest.log'
    $taskCTest = (Get-Command ctest -ErrorAction Stop).Source
    $taskCommand = '& ' + (Quote-TestLiteral $taskCTest) + ' --test-dir ' + (Quote-TestLiteral $taskDirectory) + ' --output-on-failure --timeout 120'
    if ($Filter) { $taskCommand += ' -R ' + (Quote-TestLiteral $Filter) }
    if ($Configuration) { $taskCommand += ' -C ' + (Quote-TestLiteral $Configuration) }
}
$taskCommand += ' *> ' + (Quote-TestLiteral $taskLog) + '; exit $LASTEXITCODE'
if ($ArtifactDirectory) {
    $taskArtifacts = [IO.Path]::GetFullPath($ArtifactDirectory)
    [IO.Directory]::CreateDirectory($taskArtifacts) | Out-Null
    $taskCommand = '$env:YOUNGDRAW_UI_ARTIFACT_DIR=' + (Quote-TestLiteral $taskArtifacts) + '; ' + $taskCommand
}
$taskCommand = '$env:YOUNGDRAW_UI_STAGE=' + (Quote-TestLiteral $Stage) + '; Set-Location -LiteralPath ' + (Quote-TestLiteral $taskDirectory) + '; ' + $taskCommand
$taskEncoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($taskCommand))
if ($ArtifactDirectory) {
    $taskReceipt = [ordered]@{ stage=$Stage; utc=[DateTime]::UtcNow.ToString('o'); working_directory=$taskDirectory; executable=$Executable; arguments=$Arguments; configuration=$Configuration; script_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash; isolation='new Windows desktop, never switched' }
    if ($Executable) { $taskReceipt.executable_sha256 = (Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash }
    $taskReceipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskArtifacts 'isolated-run.json') -Encoding UTF8
}
Write-Host 'Running tests on an isolated desktop (user desktop stays unchanged).'
$taskResult = [YoungDraw.IsolatedTests]::Run($taskPowerShell, '"' + $taskPowerShell + '" -NoProfile -NonInteractive -EncodedCommand ' + $taskEncoded)
if (Test-Path -LiteralPath $taskLog) { Get-Content -LiteralPath $taskLog }
if ($taskResult) { throw "Isolated test failed (exit $taskResult). Log: $taskLog" }
