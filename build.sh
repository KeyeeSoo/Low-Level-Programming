#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# The original DDS generated messages.* must be regenerated for the extended IDL.
command -v idlc >/dev/null || { echo 'Missing idlc (Cyclone DDS IDL compiler)' >&2; exit 1; }
for file in stb_image.h stb_easy_font.h; do
  [[ -f "$file" ]] || { echo "Missing $file: copy it into this directory" >&2; exit 1; }
done
idlc -l c messages.idl
CFLAGS="-std=c11 -Wall -Wextra -O1 -g"
gcc $CFLAGS server.c GameRules.c messages.c -o server \
    $(pkg-config --cflags --libs CycloneDDS) -pthread -lm
gcc $CFLAGS client.c GameRules.c Render.c Trace.c messages.c -o client \
    $(pkg-config --cflags --libs glfw3 CycloneDDS) -lGL -pthread -lm
gcc $CFLAGS test_game.c GameRules.c Trace.c -o test_game -lm
./test_game
echo 'Build succeeded. Open 3 terminals: ./server | ./client | ./client'
