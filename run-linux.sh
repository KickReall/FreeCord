#!/bin/bash
# Дев-скрипт автозапуска всех четырёх сервисов из build-linux.
# Вывод всех процессов мультиплексируется в одну консоль с цветом по сервису.
# Ctrl+C останавливает всё разом. Не для продакшена.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/build-linux"

declare -A SERVICES=(
    [auth]="services/auth/auth_service"
    [room]="services/room/room_service"
    [message]="services/message/message_service"
    [gateway]="services/gateway/gateway_service"
)
ORDER=(auth room message gateway)

declare -A COLORS=(
    [auth]="36"     # cyan
    [room]="33"     # yellow
    [message]="35"  # magenta
    [gateway]="32"  # green
)

for name in "${ORDER[@]}"; do
    exe="$BUILD_DIR/${SERVICES[$name]}"
    if [[ ! -x "$exe" ]]; then
        echo "Не найден $exe — сначала собери проект (./build-linux.sh)" >&2
        exit 1
    fi
done

LOG_DIR="$(mktemp -d)"
declare -A PIDS
declare -A POSITIONS

cleanup() {
    echo ""
    echo "Stopping services..."
    for name in "${ORDER[@]}"; do
        [[ -n "${PIDS[$name]:-}" ]] && kill "${PIDS[$name]}" 2>/dev/null
    done
    rm -rf "$LOG_DIR"
}
trap cleanup EXIT

for name in "${ORDER[@]}"; do
    exe="$BUILD_DIR/${SERVICES[$name]}"
    # exec заменяет процесс подшелла самим сервисом — $! это PID реального
    # процесса сервиса, а не обёртки, поэтому обычный kill убивает именно его.
    ( cd "$BUILD_DIR" && exec "$exe" ) > "$LOG_DIR/$name.log" 2>&1 &
    PIDS[$name]=$!
    POSITIONS[$name]=0
    sleep 0.3  # даём предыдущему сервису занять порт до старта следующего
done

echo ""
echo "Все сервисы запущены. Ctrl+C — остановить всё."
echo ""

# Опрашиваем лог-файлы вместо tail -F в фоне — не плодит дерево процессов,
# которое пришлось бы отдельно убивать при выходе.
while true; do
    for name in "${ORDER[@]}"; do
        log="$LOG_DIR/$name.log"
        [[ -f "$log" ]] || continue
        size=$(stat -c%s "$log" 2>/dev/null || echo 0)
        if (( size > POSITIONS[$name] )); then
            tail -c +"$(( POSITIONS[$name] + 1 ))" "$log" | while IFS= read -r line; do
                printf "\033[%sm%s\033[0m\n" "${COLORS[$name]}" "$line"
            done
            POSITIONS[$name]=$size
        fi

        if [[ -n "${PIDS[$name]:-}" ]] && ! kill -0 "${PIDS[$name]}" 2>/dev/null; then
            printf "\033[%sm%s\033[0m\n" "${COLORS[$name]}" "[$name] process exited"
            unset "PIDS[$name]"
        fi
    done
    sleep 0.2
done
