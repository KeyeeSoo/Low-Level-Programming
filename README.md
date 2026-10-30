# The Gingerbread Curse — ORIGINAL DDS server integration (pure C)

This version is an extension of the uploaded original `server.c` / `client.c` and `messages.idl`, **not** the separate `GameNet.idl` prototype. It retains the original DDS **JoinRequest / JoinResponse / Input / Position** topic names and the original server's approach to movement and player collision. It adds the game using **GameAction / GameState**.

**IMPORTANT:** Both new `server.c` and new `client.c` must be run together. They are NOT wire-compatible with the old executables because the IDL message schemas have been extended (including a client token). Regenerate `messages.c` and `messages.h` using the supplied `messages.idl` and rebuild both. Don't mix old/new binaries. Your originals are backed up in `original_backup/`.

## Game flow

1. Start one `server` and **two** `client` processes. Server assigns P1/P2. Duplicate join requests reuse IDs.
2. Move via WASD or arrow keys. `E` picks up a coloured ingredient square nearby. `Q` drops it.
3. Approach cauldron while holding an ingredient and press `E` to add it.
4. Correct order: **Moonflower -> Ginger Root -> Black Feather -> Sugar Glass**. Wrong ingredient = game over.
5. The player who adds each ingredient receives a rune: **pentagram, circle, flower, rectangle** (one per ingredient).
6. **Click the green dot, hold LEFT mouse, follow dots, release.** Detection checks path order, 14-pixel tolerance, 85% accuracy, 97% coverage and proximity to endpoint. Three rune failures total = game over.
7. After each rune, trace the circle to stir. Stir attempts can be retried without using a rune mistake.
8. All four stages completed = victory. Restart server and both clients to play again.

## Files

- `server.c` — based on original server DDS participant/topics and worker thread; fixes unsafe ID indexing and repeated joins, adds game events.
- `client.c` — based on original client DDS concepts; uses `Render.c` for square sprites, displays potion UI and invokes mouse tracing.
- `messages.idl` — original 4 message types **preserved and extended** with client token + 2 new game messages.
- `GameRules.c`/`.h` — original-style square movement/collision plus game state transitions; has no DDS or GLFW dependencies.
- `Trace.c`/`.h` — independent reusable pure-C mouse tracing detector.
- `Render.c`/`.h` — your existing C renderer, including PNG/JPG capability.
- `test_game.c` — unit tests for joins, invalid IDs, movement, recipe progression and trace shapes.
- `original_backup/` — copies of the original source files you supplied.

## Prerequisites

Linux: Cyclone DDS (`idlc`, `pkg-config CycloneDDS`), GLFW3, OpenGL, GCC, and pthread. Put these two headers (already used by your earlier working client) in this directory:

```bash
wget https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
wget https://raw.githubusercontent.com/nothings/stb/master/stb_easy_font.h
```

Alternatively copy the two headers from your existing project. They are not included in this ZIP.

## Build

```bash
cd Gingerbread_Original_DDS
chmod +x build.sh
./build.sh
```

This first regenerates `messages.c` + `messages.h` via `idlc -l c messages.idl` and then compiles:

```bash
gcc -std=c11 -Wall -Wextra -O1 -g server.c GameRules.c messages.c -o server \
    $(pkg-config --cflags --libs CycloneDDS) -pthread -lm

gcc -std=c11 -Wall -Wextra -O1 -g client.c GameRules.c Render.c Trace.c messages.c -o client \
    $(pkg-config --cflags --libs glfw3 CycloneDDS) -lGL -pthread -lm
```

### Run

Use **three separate terminals**, from the build folder:

```bash
./server       # Terminal 1
./client       # Terminal 2
./client       # Terminal 3
```

Close older movement-only server/client instances before starting. On different computers, all machines must be able to discover each other over DDS (same LAN or configured VPN/peers); share the *same new* `messages.idl`-generated protocol.

## Important limitations / next steps

- Runtime multiplayer integration was **not tested** here because this environment lacks Cyclone DDS + GLFW dev libraries / a window. C syntax checks against library stubs and actual native unit tests were completed.
- Movement is sent in small directional steps like the original server, and the new token prevents a second client from taking over another client's player ID by accident. DDS itself is not providing secure authentication; do not use this approach for an untrusted public internet game.
- Players who disconnect currently keep their player slots until the server restarts. Next step: add explicit Leave/Disconnect and session restart.
- The UI uses 800x600 pixel coordinates; the window is intentionally fixed-size for the first version.
- PNG textures remain available through Render.c but sprites are currently square placeholders as requested.
- Game and movement are server-authoritative; **mouse tracing is verified client-side only**. Server validates stage/active player/session when accepting pass/fail, but it cannot independently verify the actual mouse path yet.
- No animated sprite/scene art, no menus/audio, no in-game restart, no interaction between a dropped ingredient and its new drop position.
- If a crash persists, use `gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g` in both build commands and capture the sanitizer output.
