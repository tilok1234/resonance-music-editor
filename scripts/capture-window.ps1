param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
# .NET 10 moved the GDI+ image interfaces into their own assembly, so referencing
# System.Drawing alone no longer compiles this helper.
$drawingAssemblies = @([System.Drawing.Bitmap].Assembly.Location)
$drawingAssemblies += [System.AppDomain]::CurrentDomain.GetAssemblies() |
    Where-Object { $_.GetName().Name -like 'System.Private.Windows*' -and $_.Location } |
    ForEach-Object { $_.Location }
$drawingAssemblies = $drawingAssemblies | Select-Object -Unique
Add-Type -ReferencedAssemblies $drawingAssemblies -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class ResonanceWindowCapture
{
    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr window, out RECT rectangle);

    [DllImport("user32.dll")]
    private static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);

    public static void Save(IntPtr window, string outputPath)
    {
        RECT rectangle;
        if (! GetWindowRect(window, out rectangle))
            throw new InvalidOperationException("GetWindowRect failed");

        using (var bitmap = new Bitmap(rectangle.Right - rectangle.Left,
                                       rectangle.Bottom - rectangle.Top,
                                       PixelFormat.Format32bppArgb))
        using (var graphics = Graphics.FromImage(bitmap))
        {
            var deviceContext = graphics.GetHdc();
            try
            {
                if (! PrintWindow(window, deviceContext, 2))
                    throw new InvalidOperationException("PrintWindow failed");
            }
            finally
            {
                graphics.ReleaseHdc(deviceContext);
            }

            bitmap.Save(outputPath, ImageFormat.Png);
        }
    }
}
'@

$process = Get-Process -Id $ProcessId
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
$parent = [System.IO.Path]::GetDirectoryName($resolvedOutput)
[System.IO.Directory]::CreateDirectory($parent) | Out-Null
[ResonanceWindowCapture]::Save($process.MainWindowHandle, $resolvedOutput)
Get-Item -LiteralPath $resolvedOutput | Select-Object FullName,Length,LastWriteTime
