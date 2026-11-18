param([Parameter(Mandatory=$true)][string]$BuildDirectory, [string]$Filter = '')
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
$taskLog = Join-Path $taskDirectory 'isolated-ctest.log'
$taskCTest = (Get-Command ctest -ErrorAction Stop).Source
$taskPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
function Quote-TestLiteral([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
$taskCommand = '& ' + (Quote-TestLiteral $taskCTest) + ' --test-dir ' + (Quote-TestLiteral $taskDirectory) + ' --output-on-failure'
if ($Filter) { $taskCommand += ' -R ' + (Quote-TestLiteral $Filter) }
$taskCommand += ' *> ' + (Quote-TestLiteral $taskLog) + '; exit $LASTEXITCODE'
$taskEncoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($taskCommand))
Write-Host 'Running CTest on an isolated desktop (user desktop stays unchanged).'
$taskResult = [YoungDraw.IsolatedTests]::Run($taskPowerShell, '"' + $taskPowerShell + '" -NoProfile -NonInteractive -EncodedCommand ' + $taskEncoded)
if (Test-Path -LiteralPath $taskLog) { Get-Content -LiteralPath $taskLog }
if ($taskResult) { throw "Isolated CTest failed (exit $taskResult). Log: $taskLog" }
