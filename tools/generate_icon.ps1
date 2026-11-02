# Run with Windows PowerShell 5.1: powershell.exe -NoProfile -File tools/generate_icon.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.IO; using System.Drawing; using System.Drawing.Drawing2D; using System.Drawing.Imaging;
public static class YoungDrawIcon {
 public static void Save(string source,string destination) {
 int[] sizes={256,128,96,64,48,40,32,24,20,16};
 using(var original=Image.FromFile(source))using(var f=File.Create(destination))using(var w=new BinaryWriter(f)) {
 w.Write((ushort)0); w.Write((ushort)1); w.Write((ushort)sizes.Length);
 var data=new byte[sizes.Length][];
 for(int i=0;i<sizes.Length;i++)using(var b=new Bitmap(sizes[i],sizes[i],PixelFormat.Format32bppArgb)) {
 using(var g=Graphics.FromImage(b)) {g.CompositingMode=CompositingMode.SourceCopy;g.CompositingQuality=CompositingQuality.HighQuality;g.InterpolationMode=InterpolationMode.HighQualityBicubic;g.PixelOffsetMode=PixelOffsetMode.HighQuality;g.DrawImage(original,new Rectangle(0,0,sizes[i],sizes[i]));}
 using(var m=new MemoryStream()){b.Save(m,ImageFormat.Png);data[i]=m.ToArray();}
 }
 int offset=6+16*sizes.Length;
 for(int i=0;i<sizes.Length;i++){int z=sizes[i];w.Write((byte)(z==256?0:z));w.Write((byte)(z==256?0:z));w.Write((byte)0);w.Write((byte)0);w.Write((ushort)1);w.Write((ushort)32);w.Write(data[i].Length);w.Write(offset);offset+=data[i].Length;}
 foreach(var d in data)w.Write(d);
 }
 }
}
"@
$resourceDir=Join-Path $PSScriptRoot '../resources'
[YoungDrawIcon]::Save((Join-Path $resourceDir 'youngdraw-icon.png'),(Join-Path $resourceDir 'youngdraw.ico'))
