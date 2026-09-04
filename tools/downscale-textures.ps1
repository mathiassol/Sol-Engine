# Offline resample of a downloaded scene's textures. No engine change: the
# engine loads whatever the glTF's URIs point at, and a 1.3 GB directory of 4K
# maps is not something to commit or to upload 286 of at startup.
#
# Encode to the format the extension already claims. The glTF's URIs are fixed,
# so an output that changes a file's extension is an output the scene cannot
# find - and a .jpg holding PNG bytes would decode here (WIC sniffs the
# container) while lying to every other tool.
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$In,
    [Parameter(Mandatory)] [string]$Out,
    [int]$MaxEdge = 512
)

Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null

$png  = [System.Drawing.Imaging.ImageFormat]::Png
$jpeg = [System.Drawing.Imaging.ImageFormat]::Jpeg

$files = Get-ChildItem -Path $In -File |
    Where-Object { $_.Extension -match '^\.(png|jpg|jpeg)$' }
$done = 0
$bytesIn = 0L
$bytesOut = 0L
foreach ($f in $files) {
    $bytesIn += $f.Length
    $src = [System.Drawing.Image]::FromFile($f.FullName)
    $scale = [Math]::Min(1.0, $MaxEdge / [Math]::Max($src.Width, $src.Height))
    $w = [Math]::Max(1, [int]($src.Width * $scale))
    $h = [Math]::Max(1, [int]($src.Height * $scale))
    $dst = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.DrawImage($src, 0, 0, $w, $h)
    $g.Dispose()
    $fmt = if ($f.Extension -eq '.png') { $png } else { $jpeg }
    $target = Join-Path $Out $f.Name
    $dst.Save($target, $fmt)
    $dst.Dispose(); $src.Dispose()
    $bytesOut += (Get-Item -LiteralPath $target).Length
    $done++
}
$inMb = [Math]::Round($bytesIn / 1MB, 1)
$outMb = [Math]::Round($bytesOut / 1MB, 1)
Write-Host "resampled $done file(s) to max edge $MaxEdge ($inMb MB -> $outMb MB)"
