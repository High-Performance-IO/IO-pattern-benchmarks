#!/usr/bin/env bash
set -euo pipefail

start_us=${EPOCHREALTIME/./}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

export WORK_DIR="${WORK_DIR:-$SCRIPT_DIR}"
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
export BUILD_DIR
EXTRA_PRELOAD="${EXTRA_PRELOAD:-}"  # colon-separated libs prepended to LD_PRELOAD
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

for executable in producer consumer prodcons; do
    [[ -x "$BUILD_DIR/$executable" ]] || { printf 'Missing executable: %s\n' "$BUILD_DIR/$executable" >&2; exit 1; }
done

cd -- "$WORK_DIR"

rm_outputs() { # format count: remove files a stage produced/consumed
    local fmt=$1 count=$2
    for ((idx = 0; idx < count; idx++)); do
        rm -f -- "$(printf "$fmt" "$idx")"
    done
}

cleanup_files() {
    rm -f -- p0_*.dat p1_*.dat mid_*.dat
    if [[ "$TOPOLOGY" == "chain" || "$TOPOLOGY" == "broadcast" ]]; then
        rm_outputs "$OUTPUT_FILE_FORMAT" "$FILE_COUNT"
    fi
}
trap 'cleanup_files' EXIT
cleanup_files

# Interception applies only to the benchmark binaries, run in a subshell.
# File removal and everything else happens here, in the parent (unintercepted).
(
    [[ -z "$EXTRA_PRELOAD" ]] || export LD_PRELOAD="$EXTRA_PRELOAD"

    case "$TOPOLOGY" in
    chain)
        "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT"
        "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT"
        ;;
    pipeline)
        "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "p0_%d.dat"
        "$BUILD_DIR/prodcons" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --input "p0_%d.dat" --output "p1_%d.dat"
        "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "p1_%d.dat"
        ;;
    broadcast)
        "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT"
        pids=()
        for ((k = 0; k < N; k++)); do
            "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "$OUTPUT_FILE_FORMAT" &
            pids+=("$!")
        done
        for pid in "${pids[@]}"; do wait "$pid"; done
        ;;
    fanin)
        "$BUILD_DIR/producer" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --output "p0_%d.dat"
        pids=()
        for ((k = 0; k < N; k++)); do
            "$BUILD_DIR/prodcons" --pattern "$PATTERN" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --input "p0_%d.dat" --output "mid_${k}_%d.dat" &
            pids+=("$!")
        done
        for pid in "${pids[@]}"; do wait "$pid"; done
        "$BUILD_DIR/consumer" --window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --modules "$N" --output "mid_%d_%d.dat"
        ;;
    esac
)

end_us=${EPOCHREALTIME/./}
elapsed_us=$((10#$end_us - 10#$start_us))
printf 'End-to-end execution time: %d.%03d ms\n' "$((elapsed_us / 1000))" "$((elapsed_us % 1000))"