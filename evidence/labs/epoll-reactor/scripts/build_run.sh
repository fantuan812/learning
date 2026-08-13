#!/usr/bin/env bash
# build_run.sh — Linux 下编译并运行 epoll echo server（LT/ET 对照）
# 用法: bash build_run.sh [port]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-9000}"
mkdir -p "$ROOT/build" "$ROOT/results"
g++ -O2 -std=c++17 -Wall -Wextra "$ROOT/src/echo_server_epoll.cpp" -o "$ROOT/build/echo_server_epoll"

echo "== LT 模式（后台启动，30 秒后结束）=="
"$ROOT/build/echo_server_epoll" "$PORT" lt &
SRV=$!
sleep 1
printf 'hello-lt\n' | timeout 2 bash -c "exec 3<>/dev/tcp/127.0.0.1/$PORT; cat >&3; head -c 64 <&3" || true
kill $SRV 2>/dev/null || true
wait $SRV 2>/dev/null || true

echo "== ET 模式 =="
"$ROOT/build/echo_server_epoll" "$PORT" et > "$ROOT/results/epoll_et_linux.txt" 2>&1 &
SRV=$!
sleep 1
printf 'hello-et\n' | timeout 2 bash -c "exec 3<>/dev/tcp/127.0.0.1/$PORT; cat >&3; head -c 64 <&3" || true
kill $SRV 2>/dev/null || true
wait $SRV 2>/dev/null || true
echo "结果: $(cat "$ROOT/results/epoll_et_linux.txt")"
