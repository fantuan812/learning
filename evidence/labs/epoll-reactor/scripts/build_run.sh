#!/usr/bin/env bash
# Linux-only correctness experiment. Optional port argument is retained; default 0
# avoids collisions. Failure/timeout is nonzero, including through the tee pipeline.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-0}"
CXX="${CXX:-g++}"
PYTHON="${PYTHON:-python3}"
LOG="${LOG:-$ROOT/results/validation.log}"
if (( $# > 1 )); then
    printf 'usage: %s [port:0..65535]\n' "$0" >&2
    exit 2
fi
mkdir -p "$(dirname "$LOG")"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/epoll-reactor.XXXXXXXX")"
cleanup() { rm -rf -- "$BUILD"; }
trap cleanup EXIT

run() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}
validate() {
    printf 'epoll-reactor correctness validation\n'
    run date -u '+UTC %Y-%m-%dT%H:%M:%SZ'
    run uname -a
    run "$CXX" --version
    run "$PYTHON" --version
    run bash --version
    run timeout --version
    run sha256sum "$ROOT/src/echo_server_epoll.cpp" "$ROOT/src/reactor_io.hpp" \
        "$ROOT/tests/epoll_contract_test.cpp" "$ROOT/tests/tcp_integration_test.py" \
        "$ROOT/scripts/build_run.sh"
    printf 'NOTE: readiness counts and elapsed time are not performance evidence.\n'
    run timeout 30 "$CXX" -O2 -g -std=c++17 -Wall -Wextra -Wpedantic -Werror \
        "$ROOT/src/echo_server_epoll.cpp" -o "$BUILD/echo_server_epoll"
    run timeout 30 "$CXX" -O2 -g -std=c++17 -Wall -Wextra -Wpedantic -Werror \
        "$ROOT/tests/epoll_contract_test.cpp" -o "$BUILD/epoll_contract_test"
    run timeout --kill-after=3s 25s "$BUILD/epoll_contract_test"
    run timeout --kill-after=3s 120s "$PYTHON" -u "$ROOT/tests/tcp_integration_test.py" \
        "$BUILD/echo_server_epoll" --port "$PORT"
    printf 'PASS complete: 7 mechanism cases + 20 integration cases = 27 cases\n'
}
# Do not wrap validate in `if`, `!`, or `||`: that would disable errexit inside it.
validate 2>&1 | tee "$LOG"
