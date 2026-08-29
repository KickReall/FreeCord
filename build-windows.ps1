# Дев-скрипт автосборки под Windows. Кладёт всё в build-windows рядом с этим файлом,
# так что папку проекта можно свободно переносить — скрипт сам находит себя через $PSScriptRoot.
# Не для продакшена — когда дойдём до установщика, будет отдельный человеческий процесс сборки/деплоя.

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build-windows"

$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot) { $VcpkgRoot = "C:\vcpkg" }
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
$ToolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $VcpkgExe)) {
    Write-Error "vcpkg не найден в $VcpkgRoot. Установи vcpkg или укажи путь через переменную окружения VCPKG_ROOT."
    exit 1
}

# Останавливаем уже запущенные сервисы — иначе Windows держит .exe залоченным и сборка падает
Write-Host "==> Stopping running services (if any)..." -ForegroundColor Cyan
Get-Process auth_service, room_service, message_service, gateway_service -ErrorAction SilentlyContinue |
    Stop-Process -Force

Write-Host "==> Checking vcpkg dependencies (sqlite3, sqlitecpp, openssl, nlohmann-json)..." -ForegroundColor Cyan
& $VcpkgExe install sqlite3 sqlitecpp openssl nlohmann-json
if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg install failed"; exit 1 }

Write-Host "==> Configuring CMake..." -ForegroundColor Cyan
cmake -S $RepoRoot -B $BuildDir -DCMAKE_TOOLCHAIN_FILE="$ToolchainFile"
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed"; exit 1 }

Write-Host "==> Building..." -ForegroundColor Cyan
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

# Каждая сборка — чистый старт: старые БД от прошлых прогонов не должны переживать пересборку
Write-Host "==> Removing stale .db files..." -ForegroundColor Cyan
Get-ChildItem -Path $BuildDir -Filter "*.db" -File -ErrorAction SilentlyContinue | Remove-Item -Force

Write-Host ""
Write-Host "Build finished. Run from $BuildDir :" -ForegroundColor Green
Write-Host "  .\services\auth\Debug\auth_service.exe"
Write-Host "  .\services\room\Debug\room_service.exe"
Write-Host "  .\services\message\Debug\message_service.exe"
Write-Host "  .\services\gateway\Debug\gateway_service.exe"
