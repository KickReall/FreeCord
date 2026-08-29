# Дев-скрипт автозапуска всех четырёх сервисов из build-windows.
# Вывод всех процессов мультиплексируется в одну консоль с префиксом [имя].
# Ctrl+C останавливает всё разом. Не для продакшена.

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build-windows"

$Services = @(
    @{ Name = "auth";    Exe = "services\auth\Debug\auth_service.exe";       Color = "Cyan" }
    @{ Name = "room";    Exe = "services\room\Debug\room_service.exe";       Color = "Yellow" }
    @{ Name = "message"; Exe = "services\message\Debug\message_service.exe"; Color = "Magenta" }
    @{ Name = "gateway"; Exe = "services\gateway\Debug\gateway_service.exe"; Color = "Green" }
)

foreach ($svc in $Services) {
    $exePath = Join-Path $BuildDir $svc.Exe
    if (-not (Test-Path $exePath)) {
        Write-Error "Не найден $exePath — сначала собери проект (.\build-windows.ps1)"
        exit 1
    }
}

$LogDir = Join-Path $env:TEMP "freecord-run-logs"
if (Test-Path $LogDir) { Remove-Item $LogDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$Processes = @()
$Positions = @{}

foreach ($svc in $Services) {
    $exePath = Join-Path $BuildDir $svc.Exe
    $outLog = Join-Path $LogDir "$($svc.Name).out.log"
    $errLog = Join-Path $LogDir "$($svc.Name).err.log"
    New-Item -ItemType File -Path $outLog | Out-Null
    New-Item -ItemType File -Path $errLog | Out-Null

    # -NoNewWindow — процесс не открывает своё окно, весь вывод идёт в лог-файлы,
    # которые мы тут же читаем и печатаем с префиксом, вместо четырёх окон.
    $proc = Start-Process -FilePath $exePath -WorkingDirectory $BuildDir `
        -RedirectStandardOutput $outLog -RedirectStandardError $errLog `
        -NoNewWindow -PassThru

    $Processes += [PSCustomObject]@{ Name = $svc.Name; Color = $svc.Color; Process = $proc; OutLog = $outLog; ErrLog = $errLog }
    $Positions["$($svc.Name):out"] = 0
    $Positions["$($svc.Name):err"] = 0

    Start-Sleep -Milliseconds 300  # даём предыдущему сервису занять порт до старта следующего
}

Write-Host ""
Write-Host "Все сервисы запущены. Ctrl+C - остановить всё." -ForegroundColor Green
Write-Host ""

function Drain-Log {
    # Сервисы сами пишут строки вида "[auth] ...", свой текстовый префикс не добавляем —
    # только красим строку цветом сервиса, этого достаточно для разделения на глаз.
    param($LogPath, $PositionKey, $Name, $Color)

    $content = Get-Content -Path $LogPath -Raw -ErrorAction SilentlyContinue
    if ($content -and $content.Length -gt $Positions[$PositionKey]) {
        $newText = $content.Substring($Positions[$PositionKey])
        $Positions[$PositionKey] = $content.Length
        foreach ($line in ($newText -split "`r?`n")) {
            if ($line -ne "") {
                Write-Host $line -ForegroundColor $Color
            }
        }
    }
}

try {
    while ($true) {
        foreach ($p in $Processes) {
            Drain-Log -LogPath $p.OutLog -PositionKey "$($p.Name):out" -Name $p.Name -Color $p.Color
            Drain-Log -LogPath $p.ErrLog -PositionKey "$($p.Name):err" -Name $p.Name -Color $p.Color

            if ($p.Process.HasExited) {
                Write-Host "[$($p.Name)] " -ForegroundColor $p.Color -NoNewline
                Write-Host "process exited (code $($p.Process.ExitCode))" -ForegroundColor Red
            }
        }
        Start-Sleep -Milliseconds 200
    }
}
finally {
    Write-Host ""
    Write-Host "Stopping services..." -ForegroundColor Yellow
    foreach ($p in $Processes) {
        if (-not $p.Process.HasExited) {
            Stop-Process -Id $p.Process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item $LogDir -Recurse -Force -ErrorAction SilentlyContinue
}
