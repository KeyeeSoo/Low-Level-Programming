#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
command -v idlc >/dev/null || { echo 'Cyclone DDS idlc is missing.' >&2; exit 1; }
for f in stb_image.h stb_easy_font.h; do
  [[ -f "$f" ]] || { echo "Missing $f: copy it from your current working project." >&2; exit 1; }
done
idlc -l c messages.idl
# Encryption keys are created once per machine and are never committed.
[[ -f security/generated/permissions.p7s ]] || bash security/make_certs.sh
SECURITY_LIB=$(dirname "$(find /usr/lib -name libdds_security_auth.so -print -quit)")
SECURE_FLAGS="-DDDS_SECURITY_LIB_DIR=\"$SECURITY_LIB\""
FLAGS="-std=c11 -Wall -Wextra -O1 -g -pthread"
if [[ "${DEBUG_SANITIZE:-0}" == "1" ]]; then
  FLAGS+=" -fsanitize=address,undefined -fno-omit-frame-pointer"
  echo 'AddressSanitizer + UndefinedBehaviorSanitizer enabled.'
fi
# Server: main thread owns game state; one DDS worker handles networking
# Auth thread: passwords are hashed there so logins never stall the game.
gcc $FLAGS $SECURE_FLAGS server.c ServerBridge.c AuthWorker.c Accounts.c AccountsMongo.c \
    SecureDds.c GameRules.c messages.c -o server \
    $(pkg-config --cflags --libs CycloneDDS libcrypto libmongoc-1.0) -lm
# Client: main (GLFW/OpenGL) thread + DDS background thread
# Client: main (GLFW/OpenGL) thread + DDS background thread
gcc $FLAGS $SECURE_FLAGS client.c GameRules.c \
    render/Render.c render/Art.c \
    render/Draw_Background.c render/Draw_Ingredients.c \
    render/Draw_Main.c render/Draw_UI.c \
    Trace.c SecureDds.c messages.c -o client \
    $(pkg-config --cflags --libs glfw3 CycloneDDS sdl2 SDL2_mixer) -lGL -lm # Standalone tests do not depend on DDS or GLFW
gcc $FLAGS test_game.c GameRules.c Trace.c -o test_game -lm
gcc $FLAGS test_threads.c ServerBridge.c GameRules.c -o test_threads -lm
gcc $FLAGS test_recipe_preview.c GameRules.c -o test_recipe_preview -lm
# Test server-authoritative countdowns, stage transitions, and expiration.
gcc $FLAGS test_timers.c GameRules.c -o test_timers -lm
gcc $FLAGS test_accounts.c Accounts.c -o test_accounts $(pkg-config --cflags --libs libcrypto)
./test_accounts
./test_timers
./test_game
./test_threads
./test_recipe_preview
# Exercise the integrated reverse-trace -> potion action -> stir sequence.
gcc $FLAGS test_trace_integration.c GameRules.c Trace.c -o test_trace_integration -lm
./test_trace_integration
echo 'Build complete: ./server and ./client'
