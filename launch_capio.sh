#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd -- "$SCRIPT_DIR"

CONFIG="${CONFIG:-$SCRIPT_DIR/CAPIO.json}"
CAPIO_SERVER="${CAPIO_SERVER:-/home/marco/Desktop/capio/cmake-build-release/capio/server/capio_server}"
CAPIO_PRELOAD="${CAPIO_PRELOAD:-/home/marco/Desktop/capio/cmake-build-release/capio/posix/libcapio_posix.so.1.0.0}"
BUILD_DIR="${BUILD_DIR:-}"
if [[ -z "$BUILD_DIR" ]]; then
    for candidate in $(find "$SCRIPT_DIR" -maxdepth 1 -type d -iname '*build*'); do
        if [[ -x "$candidate/producer" ]] || [[ -x "$candidate/consumer" ]] || [[ -x "$candidate/prodcons" ]]; then
            BUILD_DIR="$candidate"
            break
        fi
    done
    BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
fi
CAPIO_DIR="${CAPIO_DIR:-$SCRIPT_DIR}"
EXTRA_PRELOAD="${EXTRA_PRELOAD:-}"  # colon-separated extra libs prepended to LD_PRELOAD
PATTERN="${PATTERN:-streaming}"
WINDOW_SIZE="${WINDOW_SIZE:-1024}"
FILE_SIZE="${FILE_SIZE:-1073741824}"
FILE_COUNT="${FILE_COUNT:-1}"
OUTPUT_FILE_FORMAT="${OUTPUT_FILE_FORMAT:-file_%d.dat}"
TOPOLOGY="${TOPOLOGY:-chain}"
N="${N:-2}"

case "$TOPOLOGY" in
chain|pipeline|broadcast|fanin) : ;;
*) printf 'Unknown TOPOLOGY: %s\n' "$TOPOLOGY" >&2; exit 1 ;;
esac

[[ -f "$CONFIG" ]] || { printf 'Missing CAPIO config: %s\n' "$CONFIG" >&2; exit 1; }
[[ -x "$CAPIO_SERVER" ]] || { printf 'Missing CAPIO server: %s\n' "$CAPIO_SERVER" >&2; exit 1; }
[[ -f "$CAPIO_PRELOAD" ]] || { printf 'Missing CAPIO preload library: %s\n' "$CAPIO_PRELOAD" >&2; exit 1; }
for executable in producer consumer prodcons; do
    [[ -x "$BUILD_DIR/$executable" ]] || { printf 'Missing executable: %s\n' "$BUILD_DIR/$executable" >&2; exit 1; }
done

run() {
    local app=$1
    shift
    local preload="${EXTRA_PRELOAD:+$EXTRA_PRELOAD:}$CAPIO_PRELOAD"
    CAPIO_DIR="$CAPIO_DIR" CAPIO_WORKFLOW_NAME=CAPIO CAPIO_APP_NAME="$app" LD_PRELOAD="$preload" "$@"
}

CAPIO_DIR="$CAPIO_DIR" "$CAPIO_SERVER" -c "$CONFIG" &
server_pid=$!
cleanup() {
    status=$?
    trap - EXIT
    kill -0 "$server_pid" 2>/dev/null && kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    # CAPIO writes per-run artifacts; remove them so they don't accumulate.
    rm -f "$SCRIPT_DIR"/files_location_*.txt || true
    # CAPIO leaves /dev/shm segments + named semaphores on abnormal exit; leftover
    # shm makes the NEXT server abort ("canary already exists") and wedges clients.
    rm -f /dev/shm/CAPIO* /dev/shm/sem.CAPIO* 2>/dev/null || true
    exit "$status"
}
trap cleanup EXIT
sleep 1
kill -0 "$server_pid" 2>/dev/null || { printf 'CAPIO server exited during startup\n' >&2; exit 1; }

start_us=${EPOCHREALTIME/./}

# Launch downstream stages first, the very first producer last, all in the background so CAPIO
# streams data across the whole pipeline instead of running stages one after another.
pids=()
launch() {
    "$@" &
    pids+=("$!")
}

case "$TOPOLOGY" in
chain)
    launch run consumer "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT"
    launch run producer "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT"
    ;;
pipeline)
    launch run consumer "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "p1_%d.dat"
    launch run prodcons "$BUILD_DIR/prodcons" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --input "p0_%d.dat" --output "p1_%d.dat"
    launch run producer "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "p0_%d.dat"
    ;;
broadcast)
    for ((k = 0; k < N; k++)); do
        launch run consumer "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT"
    done
    launch run producer "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT"
    ;;
fanin)
    launch run consumer "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --modules "$N" --output "mid_%d_%d.dat"
    for ((k = 0; k < N; k++)); do
        launch run prodcons "$BUILD_DIR/prodcons" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --input "p0_%d.dat" --output "mid_${k}_%d.dat"
    done
    launch run producer "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "p0_%d.dat"
    ;;
esac

for pid in "${pids[@]}"; do wait "$pid"; done

end_us=${EPOCHREALTIME/./}
elapsed_us=$((10#$end_us - 10#$start_us))
printf 'End-to-end execution time: %d.%03d ms\n' "$((elapsed_us / 1000))" "$((elapsed_us % 1000))"
