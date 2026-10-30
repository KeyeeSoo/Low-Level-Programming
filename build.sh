#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
command -v idlc >/dev/null || { echo 'Cyclone DDS idlc is missing.' >&2; exit 1; }
for f in stb_image.h stb_easy_font.h; do
  [[ -f "$f" ]] || { echo "Missing $f: copy it from your current working project." >&2; exit 1; }
done
idlc -l c messages.idl
FLAGS="-std=c11 -Wall -Wextra -O1 -g -pthread"
if [[ "${DEBUG_SANITIZE:-0}" == "1" ]]; then
  FLAGS+=" -fsanitize=address,undefined -fno-omit-frame-pointer"
  echo 'AddressSanitizer + UndefinedBehaviorSanitizer enabled.'
fi
# Server: main thread owns game state; one DDS worker handles networking
gcc $FLAGS server.c ServerBridge.c GameRules.c messages.c -o server \
    $(pkg-config --cflags --libs CycloneDDS) -lm
# Client: main (GLFW/OpenGL) thread + DDS background thread
gcc $FLAGS client.c GameRules.c Render.c Trace.c messages.c -o client \
    $(pkg-config --cflags --libs glfw3 CycloneDDS) -lGL -lm
# Standalone tests do not depend on DDS or GLFW
gcc $FLAGS test_game.c GameRules.c Trace.c -o test_game -lm
gcc $FLAGS test_threads.c ServerBridge.c GameRules.c -o test_threads -lm
./test_game
./test_threads
echo 'Build complete: ./server and ./client'
