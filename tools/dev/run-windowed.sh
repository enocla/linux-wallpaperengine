#!/usr/bin/env bash
# Runs a background windowed for a fixed time, saves a screenshot and summarizes the log.
#
# usage: tools/dev/run-windowed.sh <background id or path> [seconds] [screenshot.png] [extra args...]
#   WPE_DELAY   frames to wait before the screenshot (default 300)
#   WPE_LOG     log file (default /tmp/wpe.log)
#   WPE_BUILD   build directory (default ./build)
#
# Must be run inside the dev shell (nix develop) so the runtime libraries are found.
set -u

background="${1:?background id or path required}"
seconds="${2:-20}"
screenshot="${3:-/tmp/wpe-shot.png}"
shift $(($# < 3 ? $# : 3))

build="${WPE_BUILD:-$(git rev-parse --show-toplevel)/build}"
log="${WPE_LOG:-/tmp/wpe.log}"

rm -f "$screenshot"
cd "$build/output" || exit 1

timeout "$seconds" ./linux-wallpaperengine \
	--window 0x0x1280x720 \
	--screenshot "$screenshot" --screenshot-delay "${WPE_DELAY:-300}" \
	"$@" "$background" >"$log" 2>&1
status=$?

# 124 = stopped by timeout, which is the expected outcome
echo "exit=$status log=$log ($(wc -l <"$log") lines) screenshot=$screenshot"
echo "--- most frequent log lines ---"
sort "$log" | uniq -c | sort -rn | head -25
