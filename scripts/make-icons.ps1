<#
.SYNOPSIS
  Makes the application icons from logo.png (square, ideally 1024 px or larger).

.DESCRIPTION
  Writes
    ui\icons\voicestudio.ico  - Windows icon (exe, taskbar, tray, installer): 16..256 px. The 256 px image is stored as
                                PNG (lossless); the smaller ones as 32-bit bitmaps, which every Windows tool accepts.
    ui\icons\voicestudio.png  - 1024 px copy for the window icon and the sidebar.
  Small sizes zoom in on the centre of the logo (the microphone), so the icon stays recognisable at 16-48 px instead of
  shrinking the whole picture into a blur. Resampling is high-quality bicubic.

.EXAMPLE
  scripts\make-icons.ps1
  scripts\make-icons.ps1 -Logo path\to\other-logo.png
#>
param(
    [string]$Logo = "",
    [double]$CenterX = 0.5,   # centre of the zoom for small sizes, as a fraction of the logo's width / height
    [double]$CenterY = 0.485
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Logo) { $Logo = Join-Path $root "logo.png" }
$icons = Join-Path $root "ui\icons"

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

public static class VsIcons {
    // Square crop of `zoom` (1 = whole image) around (cx, cy), scaled to size x size.
    public static Bitmap Render(Bitmap src, int size, double zoom, double cx, double cy) {
        double side = Math.Min(src.Width, src.Height) * zoom;
        double x = Math.Max(0, Math.Min(src.Width - side, cx * src.Width - side / 2));
        double y = Math.Max(0, Math.Min(src.Height - side, cy * src.Height - side / 2));
        var dst = new Bitmap(size, size, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(dst)) {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.SmoothingMode = SmoothingMode.HighQuality;
            g.CompositingQuality = CompositingQuality.HighQuality;
            using (var attrs = new ImageAttributes()) {
                attrs.SetWrapMode(WrapMode.TileFlipXY); // no dark fringe at the edges
                g.DrawImage(src, new Rectangle(0, 0, size, size), (float)x, (float)y, (float)side, (float)side,
                            GraphicsUnit.Pixel, attrs);
            }
        }
        return dst;
    }

    static byte[] Png(Bitmap b) {
        using (var ms = new MemoryStream()) { b.Save(ms, ImageFormat.Png); return ms.ToArray(); }
    }

    // 32-bit BGRA DIB as stored in .ico files: BITMAPINFOHEADER (height doubled), bottom-up pixels, AND mask.
    static byte[] Dib(Bitmap b) {
        int w = b.Width, h = b.Height, maskStride = ((w + 31) / 32) * 4;
        using (var ms = new MemoryStream())
        using (var bw = new BinaryWriter(ms)) {
            bw.Write(40); bw.Write(w); bw.Write(h * 2); bw.Write((short)1); bw.Write((short)32);
            bw.Write(0); bw.Write(w * h * 4 + maskStride * h); bw.Write(0); bw.Write(0); bw.Write(0); bw.Write(0);
            for (int y = h - 1; y >= 0; --y)
                for (int x = 0; x < w; ++x) {
                    Color c = b.GetPixel(x, y);
                    bw.Write(c.B); bw.Write(c.G); bw.Write(c.R); bw.Write(c.A);
                }
            bw.Write(new byte[maskStride * h]); // all opaque
            return ms.ToArray();
        }
    }

    public static void WriteIco(string path, List<Bitmap> images) {
        var data = new List<byte[]>();
        foreach (var b in images) data.Add(b.Width >= 256 ? Png(b) : Dib(b));
        using (var fs = File.Create(path))
        using (var bw = new BinaryWriter(fs)) {
            bw.Write((short)0); bw.Write((short)1); bw.Write((short)images.Count);
            int offset = 6 + 16 * images.Count;
            for (int i = 0; i < images.Count; ++i) {
                int s = images[i].Width;
                bw.Write((byte)(s >= 256 ? 0 : s)); bw.Write((byte)(s >= 256 ? 0 : s));
                bw.Write((byte)0); bw.Write((byte)0); bw.Write((short)1); bw.Write((short)32);
                bw.Write(data[i].Length); bw.Write(offset);
                offset += data[i].Length;
            }
            foreach (var d in data) bw.Write(d);
        }
    }
}
"@

$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Logo))
try {
    if ($src.Width -ne $src.Height) { Write-Warning "logo is not square ($($src.Width) x $($src.Height)); using the centre square" }

    # Zoom per icon size: the whole logo from 128 px up, closer in below so the microphone stays readable.
    $plan = [ordered]@{ 16 = 0.42; 20 = 0.42; 24 = 0.45; 32 = 0.5; 40 = 0.54; 48 = 0.58; 64 = 0.68; 96 = 0.85; 128 = 1.0; 256 = 1.0 }
    $list = New-Object 'System.Collections.Generic.List[System.Drawing.Bitmap]'
    foreach ($entry in $plan.GetEnumerator()) { # (an ordered dictionary's [int] indexer is by position, not key)
        $list.Add([VsIcons]::Render($src, [int]$entry.Key, [double]$entry.Value, $CenterX, $CenterY))
    }
    [VsIcons]::WriteIco((Join-Path $icons "voicestudio.ico"), $list)
    foreach ($b in $list) { $b.Dispose() }

    $big = [Math]::Min(1024, [Math]::Min($src.Width, $src.Height))
    $png = [VsIcons]::Render($src, $big, 1.0, 0.5, 0.5)
    $png.Save((Join-Path $icons "voicestudio.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $png.Dispose()
} finally {
    $src.Dispose()
}
Get-Item (Join-Path $icons "voicestudio.ico"), (Join-Path $icons "voicestudio.png") | Select-Object Name, Length
