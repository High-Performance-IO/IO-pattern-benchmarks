#!/usr/bin/env bash
# One CAPIO run of a topology on this node: the local counterpart of the Slurm job
# (clip's benchmarks/slurm/capio_job.sh), which does the same with one server per node.
# Applications, in launch order:
#   chain      producer, consumer
#   pipeline   producer, prodcons, consumer
#   broadcast  producer, N consumer
#   fanin      producer, N prodcons, consumer
#
# Stages start upstream first; each downstream one once CAPIO has registered its
# inputs (see wait_registered), so no consumer looks up a file before it exists.
#
# Prints "End-to-end execution time: <ms> ms": the binaries' own execution, from the
# first one starting to the last one ending, each timed right around the binary.
# Starting the server is not included; the wall time, which includes waiting for
# registrations, is printed apart.
#
# Environment:
#   CAPIO_SERVER CAPIO_PRELOAD CONFIG CAPIO_DIR BUILD_DIR
#   CAPIO_BACKEND  server backend (default: the server's own)
#   EXTRA_PRELOAD  colon-separated libs prepended to LD_PRELOAD of the binaries
#   SERVER_WAIT    seconds to wait for the server to start (default 120)
#   REGISTER_WAIT  seconds a stage waits for its inputs to be registered (default 10)
#   TOPOLOGY N PATTERN WINDOW_SIZE FILE_SIZE FILE_COUNT COMPUTE OUTPUT_FILE_FORMAT
set -euo pipefail

# Microseconds since the epoch. EPOCHREALTIME needs bash 5; RHEL 8 has 4.4,
# so fall back to GNU date there.
now_us() {
    if [[ -n ${EPOCHREALTIME:-} ]]; then echo "${EPOCHREALTIME/./}"; else date +%s%6N; fi
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

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
CAPIO_BACKEND="${CAPIO_BACKEND:-}"
EXTRA_PRELOAD="${EXTRA_PRELOAD:-}"
SERVER_WAIT="${SERVER_WAIT:-120}"
PATTERN="${PATTERN:-streaming}"
WINDOW_SIZE="${WINDOW_SIZE:-1024}"
FILE_SIZE="${FILE_SIZE:-1073741824}"
FILE_COUNT="${FILE_COUNT:-1}"
OUTPUT_FILE_FORMAT="${OUTPUT_FILE_FORMAT:-file_%d.dat}"
TOPOLOGY="${TOPOLOGY:-chain}"
N="${N:-2}"
COMPUTE="${COMPUTE:-0}"   # djb2 hash passes per read/write chunk

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

# CAPIO leaves /dev/shm segments + named semaphores on abnormal exit; leftover shm makes
# the next server abort ("canary already exists"), so clear them before and after.
clean_shm() { rm -f /dev/shm/CAPIO* /dev/shm/sem.CAPIO* 2>/dev/null || true; }

cd -- "$CAPIO_DIR"
rm -f files_location_*.txt
clean_shm

RUN_DIR=$(mktemp -d "${TMPDIR:-/tmp}/launch_capio.XXXXXX") # server log and binary timings
SERVER_LOG="$RUN_DIR/capio_server.log"
CAPIO_DIR="$CAPIO_DIR" "$CAPIO_SERVER" -c "$CONFIG" -r "$CAPIO_DIR" \
    ${CAPIO_BACKEND:+--backend "$CAPIO_BACKEND"} >"$SERVER_LOG" 2>&1 &
server_pid=$!

cleanup() {
    status=$?
    trap - EXIT
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    rm -f "$CAPIO_DIR"/files_location_*.txt || true
    clean_shm
    rm -rf "$RUN_DIR"
    exit "$status"
}
trap cleanup EXIT

# the server prints this once it has opened its location file
deadline=$((SECONDS + SERVER_WAIT))
until grep -q 'Server instance successfully started' "$SERVER_LOG"; do
    if ! kill -0 "$server_pid" 2>/dev/null; then
        printf 'CAPIO server exited during startup:\n' >&2
        tail -n 20 "$SERVER_LOG" >&2
        exit 1
    fi
    if ((SECONDS >= deadline)); then
        printf 'CAPIO server not up after %ds:\n' "$SERVER_WAIT" >&2
        tail -n 20 "$SERVER_LOG" >&2
        exit 1
    fi
    sleep 0.5
done
sleep 1 # the server threads start right after the message

TIMES="$RUN_DIR/times" # one file per application: "<start µs> <end µs>" of its binary
mkdir -p "$TIMES"
# Times the binary alone. It runs in its own bash, which does not see now_us, so it
# defines the same fallback.
TIMER='now() { if [[ -n ${EPOCHREALTIME:-} ]]; then echo "${EPOCHREALTIME/./}"; else date +%s%6N; fi; }
s=$(now); rc=0; "${@:2}" || rc=$?; printf "%s %s\n" "$s" "$(now)" >"$1"; exit "$rc"'

pids=()
launch() { # app binary args...: one application, in the background
    local app=$1
    shift
    bash -c "$TIMER" timer "$TIMES/${#pids[@]}_$app" \
        /usr/bin/env CAPIO_DIR="$CAPIO_DIR" CAPIO_WORKFLOW_NAME=CAPIO CAPIO_APP_NAME="$app" \
        LD_PRELOAD="${EXTRA_PRELOAD:+$EXTRA_PRELOAD:}$CAPIO_PRELOAD" "$@" &
    pids+=("$!")
}

common=(--window "$WINDOW_SIZE" --size "$FILE_SIZE" --count "$FILE_COUNT" --compute "$COMPUTE")

# A downstream stage starts only once its input is registered: the server lists each new
# file in files_location_<node>.txt (in CAPIO_DIR). Waiting here overlaps the upstream
# stage's run, so it is not added to the measured time, which starts with the first binary.
registered() { # name-format: whether the server listed file 0 of it
    local name
    # shellcheck disable=SC2059 # the format is the benchmark's own %d file-name format
    name=$(printf "$1" 0)
    grep -qsF -- "/$name " "$CAPIO_DIR"/files_location_*.txt
}

wait_registered() { # name-format...: until the server listed all of them
    local format waited_us since_us
    since_us=$(now_us)
    local deadline=$((SECONDS + ${REGISTER_WAIT:-10}))
    for format; do
        until registered "$format"; do
            if ((SECONDS >= deadline)); then
                printf '  warning: %s not registered after %ss; starting anyway\n' \
                    "$format" "${REGISTER_WAIT:-10}" >&2
                return 0
            fi
            sleep 0.002
        done
    done
    waited_us=$((10#$(now_us) - 10#$since_us))
    printf '  inputs registered after %d.%03d ms\n' "$((waited_us / 1000))" "$((waited_us % 1000))"
}

start_us=$(now_us)

# Upstream stages first; each downstream stage once its inputs are registered (see above).
case "$TOPOLOGY" in
chain)
    launch producer "$BUILD_DIR/producer" --pattern "$PATTERN" "${common[@]}" --output "$OUTPUT_FILE_FORMAT"
    wait_registered "$OUTPUT_FILE_FORMAT"
    launch consumer "$BUILD_DIR/consumer" "${common[@]}" --output "$OUTPUT_FILE_FORMAT"
    ;;
pipeline)
    launch producer "$BUILD_DIR/producer" --pattern "$PATTERN" "${common[@]}" --output "p0_%d.dat"
    wait_registered "p0_%d.dat"
    launch prodcons "$BUILD_DIR/prodcons" --pattern "$PATTERN" "${common[@]}" --input "p0_%d.dat" --output "p1_%d.dat"
    wait_registered "p1_%d.dat"
    launch consumer "$BUILD_DIR/consumer" "${common[@]}" --output "p1_%d.dat"
    ;;
broadcast)
    launch producer "$BUILD_DIR/producer" --pattern "$PATTERN" "${common[@]}" --output "$OUTPUT_FILE_FORMAT"
    wait_registered "$OUTPUT_FILE_FORMAT"
    for ((k = 0; k < N; k++)); do
        launch consumer "$BUILD_DIR/consumer" "${common[@]}" --output "$OUTPUT_FILE_FORMAT"
    done
    ;;
fanin)
    launch producer "$BUILD_DIR/producer" --pattern "$PATTERN" "${common[@]}" --output "p0_%d.dat"
    wait_registered "p0_%d.dat"
    for ((k = 0; k < N; k++)); do
        launch prodcons "$BUILD_DIR/prodcons" --pattern "$PATTERN" "${common[@]}" --input "p0_%d.dat" --output "mid_${k}_%d.dat"
    done
    for ((k = 0; k < N; k++)); do
        wait_registered "mid_${k}_%d.dat"
    done
    launch consumer "$BUILD_DIR/consumer" "${common[@]}" --modules "$N" --output "mid_%d_%d.dat"
    ;;
esac

failed=0
for pid in "${pids[@]}"; do wait "$pid" || failed=1; done

end_us=$(now_us)
wall_us=$((10#$end_us - 10#$start_us))
((failed == 0)) || { printf 'an application failed\n' >&2; exit 1; }
if (($(find "$TIMES" -type f | wc -l) != ${#pids[@]})); then
    printf 'missing binary timings in %s\n' "$TIMES" >&2
    exit 1
fi
first=$(cut -d' ' -f1 "$TIMES"/* | sort -n | head -n 1) # as text: awk may print µs in e-notation
last=$(cut -d' ' -f2 "$TIMES"/* | sort -n | tail -n 1)
elapsed_us=$((10#$last - 10#$first))
# Per application: when its binary started (from the first one) and how long it ran, to see
# whether the stages overlapped (streaming) and which one sets the end time.
for f in "$TIMES"/*; do
    read -r s e <"$f"
    printf '  %-12s started +%d.%03d ms, ran %d.%03d ms, ended +%d.%03d ms\n' "${f##*/}" \
        $(((s - first) / 1000)) $(((s - first) % 1000)) $(((e - s) / 1000)) $(((e - s) % 1000)) \
        $(((e - first) / 1000)) $(((e - first) % 1000))
done
printf 'Wall time including the registration waits: %d.%03d ms\n' "$((wall_us / 1000))" "$((wall_us % 1000))"
printf 'End-to-end execution time: %d.%03d ms\n' "$((elapsed_us / 1000))" "$((elapsed_us % 1000))"
