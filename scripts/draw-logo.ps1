<#
.SYNOPSIS
  Draws logo.png: a white studio microphone flanked by waveform bars on an indigo gradient tile.

.DESCRIPTION
  Flat, vector-style mark in the app's accent colours (Theme.accent #6E7CFF), with transparent rounded corners, so it
  reads cleanly from the 16 px tray icon up to the installer. Run scripts\make-icons.ps1 afterwards to refresh
  ui\icons\voicestudio.ico / .png.

.EXAMPLE
  scripts\draw-logo.ps1
  scripts\draw-logo.ps1 -Out path\to\logo.png -Size 2048
#>
param(
    [string]$Out = "",
    [int]$Size = 1024
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $root "logo.png" }

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

public static class VsLogo {
    static GraphicsPath RoundRect(float x, float y, float w, float h, float r) {
        var p = new GraphicsPath();
        float d = r * 2;
        p.AddArc(x, y, d, d, 180, 90);
        p.AddArc(x + w - d, y, d, d, 270, 90);
        p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        p.AddArc(x, y + h - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    static Pen RoundPen(Color c, float width) {
        var pen = new Pen(c, width);
        pen.StartCap = LineCap.Round;
        pen.EndCap = LineCap.Round;
        pen.LineJoin = LineJoin.Round;
        return pen;
    }

    // Drawn on a 1024 unit grid, scaled to `size`.
    public static Bitmap Draw(int size) {
        var bmp = new Bitmap(size, size, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(bmp)) {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.CompositingQuality = CompositingQuality.HighQuality;
            g.Clear(Color.Transparent);
            g.ScaleTransform(size / 1024f, size / 1024f);

            // Tile: indigo gradient, with a soft light from the top for depth.
            using (var tile = RoundRect(0, 0, 1024, 1024, 228)) {
                using (var bg = new LinearGradientBrush(new PointF(0, 0), new PointF(1024, 1024),
                                                        Color.FromArgb(0x7E, 0x8B, 0xFF), Color.FromArgb(0x38, 0x29, 0xBD))) {
                    g.FillPath(bg, tile);
                }
                g.SetClip(tile);
                using (var glow = new GraphicsPath()) {
                    glow.AddEllipse(-260, -560, 1544, 1100);
                    using (var gb = new PathGradientBrush(glow)) {
                        gb.CenterColor = Color.FromArgb(48, 255, 255, 255);
                        gb.SurroundColors = new[] { Color.FromArgb(0, 255, 255, 255) };
                        g.FillPath(gb, glow);
                    }
                }
                g.ResetClip();
                using (var rim = new Pen(Color.FromArgb(40, 255, 255, 255), 6)) { // fine inner edge highlight
                    using (var inner = RoundRect(3, 3, 1018, 1018, 225)) g.DrawPath(rim, inner);
                }
            }

            var white = Color.FromArgb(255, 255, 255, 255);
            var cx = 512f;

            // Soft shadow under the microphone so it lifts off the tile.
            using (var shadow = RoundRect(cx - 118, 196, 236, 420, 118))
            using (var sb = new SolidBrush(Color.FromArgb(45, 20, 10, 90))) {
                var m = new Matrix(); m.Translate(0, 14); shadow.Transform(m);
                g.FillPath(sb, shadow);
            }

            // Capsule with grille slots.
            using (var capsule = RoundRect(cx - 112, 180, 224, 420, 112))
            using (var wb = new SolidBrush(white)) {
                g.FillPath(wb, capsule);
            }
            using (var slot = RoundPen(Color.FromArgb(0x5B, 0x55, 0xE6), 26)) {
                g.DrawLine(slot, cx - 50, 300, cx + 50, 300);
                g.DrawLine(slot, cx - 50, 360, cx + 50, 360);
                g.DrawLine(slot, cx - 50, 420, cx + 50, 420);
            }

            // Yoke, stem and base.
            using (var stroke = RoundPen(white, 50)) {
                g.DrawArc(stroke, cx - 180, 340, 360, 360, 0, 180);
                g.DrawLine(stroke, cx, 700, cx, 812);
                g.DrawLine(stroke, cx - 120, 830, cx + 120, 830);
            }

            // Waveform bars either side.
            float[] offsets = { 262, 344 };
            float[] heights = { 300, 160 };
            int[] alphas = { 235, 170 };
            for (int i = 0; i < offsets.Length; ++i) {
                using (var bar = RoundPen(Color.FromArgb(alphas[i], 255, 255, 255), 50)) {
                    float half = heights[i] / 2, y = 470;
                    g.DrawLine(bar, cx - offsets[i], y - half, cx - offsets[i], y + half);
                    g.DrawLine(bar, cx + offsets[i], y - half, cx + offsets[i], y + half);
                }
            }
        }
        return bmp;
    }
}
"@

$bmp = [VsLogo]::Draw($Size)
try {
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $bmp.Dispose()
}
Get-Item $Out | Select-Object Name, Length
