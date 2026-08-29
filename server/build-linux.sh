#!/bin/bash
# Дев-скрипт автосборки под Linux/WSL. Кладёт всё в build-linux рядом с этим файлом,
# так что папку проекта можно свободно переносить — скрипт сам находит себя.
# Не для продакшена — когда дойдём до установщика, будет отдельный человеческий процесс сборки/деплоя.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/build-linux"

# CMake configure падает на 9p ("Operation not permitted"), если проект лежит на
# примонтированном Windows-диске внутри WSL — сборка обязана идти в родной ФС Linux.
if [[ "$REPO_ROOT" == /mnt/* ]]; then
    echo "ОШИБКА: проект лежит под $REPO_ROOT (примонтированный диск Windows)." >&2
    echo "CMake здесь падает на 9p. Скопируй папку проекта в файловую систему WSL" >&2
    echo "(например, ~/FreeCord) и запусти скрипт оттуда." >&2
    exit 1
fi

VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
VCPKG_EXE="$VCPKG_ROOT/vcpkg"
TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

if [[ ! -x "$VCPKG_EXE" ]]; then
    echo "vcpkg не найден в $VCPKG_ROOT. Установи vcpkg или укажи путь через переменную VCPKG_ROOT." >&2
    exit 1
fi

# Останавливаем уже запущенные сервисы — не мешает пересборке напрямую (Linux не
# блокирует запущенный exe), но иначе после сборки останутся старые процессы на портах.
echo "==> Stopping running services (if any)..."
pkill -f '(auth|room|message|gateway)_service' 2>/dev/null || true

echo "==> Checking vcpkg dependencies (sqlite3, sqlitecpp, openssl, nlohmann-json)..."
"$VCPKG_EXE" install sqlite3 sqlitecpp openssl nlohmann-json

echo "==> Configuring CMake..."
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_BUILD_TYPE=Debug

echo "==> Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Каждая сборка — чистый старт: старые БД от прошлых прогонов не должны переживать пересборку
echo "==> Removing stale .db files..."
find "$BUILD_DIR" -maxdepth 1 -name '*.db' -delete

echo ""
echo "Build finished. Run from $BUILD_DIR:"
echo "  ./services/auth/auth_service"
echo "  ./services/room/room_service"
echo "  ./services/message/message_service"
echo "  ./services/gateway/gateway_service"
