#!/bin/bash
# Runs Cemu with SIGPIPE ignored.
#
# Cemu 2.6.0 on macOS dies with signal 13 (SIGPIPE) as soon as a guest socket
# write hits a closed or reset connection — the first multiplayer packet is
# enough. Ignoring SIGPIPE in the parent process survives exec, which keeps
# Cemu alive and lets the guest see the real socket error instead.
#
# Usage: tools/wiiu/cemu-run.sh <game.wuhb|game.rpx> [extra Cemu args...]
#
# Environment overrides:
#   WIIU_CEMU  Cemu binary  (default: /Applications/Cemu.app/Contents/MacOS/Cemu)
#   WIIU_MLC   Cemu MLC     (default: /Users/mario/work/wiiu/cemu-mlc)
set -eu
CEMU=${WIIU_CEMU:-/Applications/Cemu.app/Contents/MacOS/Cemu}
MLC=${WIIU_MLC:-/Users/mario/work/wiiu/cemu-mlc}

if [ $# -lt 1 ]; then
  echo "usage: $0 <game.wuhb|game.rpx> [extra Cemu args...]" >&2
  exit 2
fi

python3 -c 'import os, signal, sys; signal.signal(signal.SIGPIPE, signal.SIG_IGN); os.execv(sys.argv[1], sys.argv[1:])' \
  "$CEMU" -g "$1" -m "$MLC" "${@:2}"
