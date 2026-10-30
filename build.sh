#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if ! command -v idlc >/dev/null 2>&1; then
  echo 'Missing idlc (Cyclone DDS IDL compiler). Install Cyclone DDS development tools.' >&2
  exit 1
fi
if ! pkg-config --exists glfw3 CycloneDDS; then
  echo 'Missing GLFW3 or CycloneDDS development pkg-config entries.' >&2
  exit 1
fi
idlc -l c GameNet.idl
DDS_FLAGS=$(pkg-config --cflags --libs CycloneDDS)
CLIENT_FLAGS=$(pkg-config --cflags --libs glfw3 CycloneDDS)
# Deliberately split flags on shell whitespace like standard pkg-config output.
# shellcheck disable=SC2086
gcc -std=c11 -Wall -Wextra -O2 game_server.c GameLogic.c GameNet.c -o potion_server $DDS_FLAGS -lm
# shellcheck disable=SC2086
gcc -std=c11 -Wall -Wextra -O2 game_client.c Render.c Trace.c GameLogic.c GameNet.c -o potion_client $CLIENT_FLAGS -lGL -lm
gcc -std=c11 -Wall -Wextra -O2 test_game.c GameLogic.c -o test_game -lm
./test_game
printf '\nBuild complete. Run ./potion_server, then ./potion_client in two terminals.\n'
