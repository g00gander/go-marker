#!/bin/sh
# Stop a running simulator gracefully (exit 0), then start a fresh one.
# usage: ./restart.sh [nav_sim options]
cd "$(dirname "$0")" || exit 1
if pgrep -x nav_sim >/dev/null 2>&1; then
    pkill -TERM -x nav_sim
    for i in 1 2 3 4 5 6 7 8 9 10; do
        pgrep -x nav_sim >/dev/null 2>&1 || break
        sleep 0.3
    done
fi
exec ./build/nav_sim "$@"
