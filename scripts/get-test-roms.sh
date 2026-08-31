#!/usr/bin/env bash
# Downloads Timendus's chip8-test-suite ROMs into roms/.
#
# These are NOT part of this project and are not committed here: they are
# GPL-3.0 licensed and belong to their author. See:
#   https://github.com/Timendus/chip8-test-suite
set -euo pipefail

BASE="https://raw.githubusercontent.com/Timendus/chip8-test-suite/main/bin"
ROMS=(
  1-chip8-logo
  2-ibm-logo
  3-corax+
  4-flags
  5-quirks
  6-keypad
  7-beep
  8-scrolling
)

cd "$(dirname "$0")/.."
mkdir -p roms

for rom in "${ROMS[@]}"; do
  if curl -sfL -o "roms/${rom}.ch8" "${BASE}/${rom}.ch8"; then
    echo "  ok    roms/${rom}.ch8"
  else
    echo "  FAIL  ${rom}.ch8" >&2
    exit 1
  fi
done

echo
echo "Done. Try:  ./run_rom roms/3-corax+.ch8 20000"
