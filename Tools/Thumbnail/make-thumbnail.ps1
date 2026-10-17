# La miniatura de Shipfulness, renderizada por el motor (ver ThumbnailDirector.cpp).
#
#   .\Tools\Thumbnail\make-thumbnail.ps1                       # thumbnail.cfg -> out\
#   .\Tools\Thumbnail\make-thumbnail.ps1 -Config otro.cfg -Name prueba
#
# Por que una COPIA del proyecto y no el proyecto: el director reemplaza al
# script del juego en el nodo "Juego", y eso no puede llegar ni a la escena de
# verdad ni al Build. La copia vive en Intermediate\Thumbnail (fuera de git) y
# ademas deja abrir el Editor sin chocar con el candado de uno ya abierto
# sobre el juego.
#
# Deja tres imagenes en out\: <Name>.png (1920x1080), <Name>-square.png
# (el recorte cuadrado central, 1080x1080) y <Name>-itch.png (630x500, la
# portada de itch.io, del mismo centro).

param(
    [string]$Config = "$PSScriptRoot\thumbnail.cfg",
    [string]$Name = "thumbnail",
    [string]$Editor = "$PSScriptRoot\..\..\..\..\build\bin\Release\Editor.exe"
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$work = Join-Path $project 'Intermediate\Thumbnail\proj'
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $out | Out-Null
if (-not (Test-Path $Editor)) { throw "No esta el Editor en $Editor (.\build.cmd release en la raiz del motor)." }
$Editor = (Resolve-Path $Editor).Path

# La copia, incremental (robocopy solo copia lo que cambio). Sin lo que el motor
# genera ni esta carpeta: Assets, Scenes y la configuracion es todo lo que
# hace falta para que el Editor abra el proyecto.
robocopy $project $work /MIR /XD "$project\Build" "$project\Intermediate" "$project\.git" "$project\.claude" "$project\Tools" "$project\Minimal Engine" /XF imgui.ini .strace-editor.lock /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy fallo ($LASTEXITCODE)." }

$scripts = Join-Path $work 'Assets\Scripts\_Thumbnail'
New-Item -ItemType Directory -Force $scripts | Out-Null
Copy-Item "$PSScriptRoot\ThumbnailDirector.cpp" $scripts -Force
Copy-Item $Config (Join-Path $work 'thumbnail.cfg') -Force

# El director en lugar del juego. Por texto y no por JSON: la escena pesa 2 MB
# y ConvertTo-Json de PowerShell 5 la reescribiria entera (y trunca la
# profundidad). Si el reemplazo no encuentra nada, mejor saberlo aqui que
# capturar el menu del juego.
$scene = Join-Path $work 'Scenes\Main.strscene'
$text = [IO.File]::ReadAllText($scene)
$patched = [regex]::Replace($text, '"scripts":\s*\[\s*"ShipfulnessGame"\s*\]', '"scripts": ["ThumbnailDirector"]')
if ($patched -eq $text -and $text -notmatch '"ThumbnailDirector"') { throw 'No encontre "ShipfulnessGame" en la escena.' }
[IO.File]::WriteAllText($scene, $patched)

# Cuantos frames: los segundos de simulacion del .cfg (el director avanza 1/60
# por frame, fijo) y un margen para que la escena congelada se asiente.
$run = 2.0
foreach ($line in Get-Content $Config) {
    if ($line -match '^\s*run\s*=\s*([0-9.]+)') { $run = [double]$Matches[1] }
}
$frames = [int]([math]::Ceiling($run * 60)) + 90

$png = Join-Path $out "$Name.png"
if (Test-Path $png) { Remove-Item $png }
$argList = @("`"$work\Shipfulness.strproj`"", '--play', '--frames', "$frames", '--no-gizmos',
          '--shot-game', "`"$png`"", '--game-size', '1920x1080')
$p = Start-Process $Editor -ArgumentList $argList -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "El Editor salio con $($p.ExitCode) (ver %LOCALAPPDATA%\STRACE\logs\Editor.log)." }

# Los recortes, del CENTRO: el encuadre esta pensado para que lo importante
# (titulo, barco, choque) quepa en el cuadrado central.
Add-Type -AssemblyName System.Drawing
function Save-Crop([string]$src, [string]$dst, [int]$cw, [int]$ch, [int]$w, [int]$h) {
    $img = [Drawing.Image]::FromFile($src)
    try {
        $bmp = New-Object Drawing.Bitmap $w, $h
        $g = [Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $sx = [int](($img.Width - $cw) / 2); $sy = [int](($img.Height - $ch) / 2)
        $g.DrawImage($img, (New-Object Drawing.Rectangle 0, 0, $w, $h), (New-Object Drawing.Rectangle $sx, $sy, $cw, $ch), [Drawing.GraphicsUnit]::Pixel)
        $g.Dispose()
        $bmp.Save($dst, [Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    } finally { $img.Dispose() }
}
Save-Crop $png (Join-Path $out "$Name-square.png") 1080 1080 1080 1080
# 630x500 es 1,26: del alto entero, 1361 de ancho.
Save-Crop $png (Join-Path $out "$Name-itch.png") 1361 1080 630 500
Write-Host "Listo: $out\$Name.png (+ -square, -itch)"
