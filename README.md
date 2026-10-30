# The Gingerbread Curse — playable first prototype (pure C + DDS)

This prototype is based on the supplied concept and the original `client.c` architecture:
**Cyclone DDS** for client/server networking, **GLFW + legacy OpenGL** for the
window and drawing, and **C** for everything. Sprites are coloured squares.

**Important:** the original `client.c` and existing movement-only server do not
understand the potion/game messages. This is a **separate playable version**,
with a replacement `game_client.c` and `game_server.c` that use the new
`GameNet.idl`. Your old files are not overwritten.

## What's implemented

- Maximum 2 connected players, assigned P1/P2 via DDS join/welcome topics.
- Both see the same authoritative positions, carried items and game stage.
- Move using **W A S D**, press **E** to pick up an ingredient nearby, and E
  next to the cauldron to add it. **Q** drops the carried ingredient.
- Correct recipe: Moonflower -> Ginger Root -> Black Feather -> Sugar Glass.
- Adding the wrong ingredient ends the game.
- Each correctly added ingredient opens a rune tracing minigame for the player
  who added it: pentagram (crossing star), circle, flower, rectangle.
- Start on the green dot, **hold left mouse**, follow the dotted path, release.
  The detector checks sequential progress, tolerance and distance travelled.
- Wrong rune tracing 3 times in total ends the game. A successful rune opens
  a circular **stirring** minigame; stirring must be completed to proceed.
- Completing all four rounds wins the game.
- The server is authoritative about game progression and ingredients. Trace
  recognition currently runs on the player's local client, and the client
  reports pass/fail to the server. This is fine for an early prototype but
  is not cheat-resistant.

## Files

- `game_client.c`: window, input, user interface, DDS subscriber/publisher.
- `game_server.c`: DDS authoritative server, receives requests, broadcasts state.
- `GameLogic.h/.c`: recipe, movement, interactions and win/lose conditions.
- `GameNet.idl`: DDS message definitions (generate GameNet.h/.c with `idlc`).
- `Trace.h/.c`: shape generator, mouse tracking and detection.
- `Render.h/.c`: OpenGL square and dotted-line rendering, optional PNG textures.
- `test_game.c`, `test_rune_paths.c`: tests without DDS/GLFW.
- `build.sh`: compiler commands.

## Prerequisites

1. Linux setup with GCC, GLFW3, OpenGL and Cyclone DDS (you already used these
   for your original client). Cyclone DDS **idlc** must also be installed.
2. Place `stb_image.h` and `stb_easy_font.h` in this directory. They are small
   public single-header libraries. If you already have them in your original
   project, copy them from there. Otherwise use:

   ```bash
   wget https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
   wget https://raw.githubusercontent.com/nothings/stb/master/stb_easy_font.h
   ```

## Build

From the folder containing these files:

```bash
chmod +x build.sh
./build.sh
```

Or compile manually:

```bash
idlc -l c GameNet.idl

gcc -std=c11 game_server.c GameLogic.c GameNet.c -o potion_server \
    $(pkg-config --cflags --libs CycloneDDS) -lm

gcc -std=c11 game_client.c Render.c Trace.c GameLogic.c GameNet.c -o potion_client \
    $(pkg-config --cflags --libs glfw3 CycloneDDS) -lGL -lm
```

**Do not also compile your old `client.c` or `messages.c` into these executables.**
The game uses `GameNet.c` generated from `GameNet.idl` instead.

## Run

Terminal 1:

```bash
./potion_server
```

Terminals 2 and 3 (or two computers on the same LAN):

```bash
./potion_client
```

When two clients are connected, they can move, pick ingredients, and complete
rune/stir stages. A single computer can run both client windows as a test.
For different Wi-Fi networks, DDS usually needs explicit peer configuration,
e.g. a VPN plus Cyclone DDS XML peer configuration.

## Adjustable values

- Rune tolerance / scoring: `Trace.h` constants (`TRACE_TOLERANCE_PIXELS`,
  `TRACE_MIN_ACCURACY`, `TRACE_MIN_COMPLETION`).
- Ingredient positions, cauldron and movement speed: `GameLogic.c`.
- Rune appearance, custom pentagram and stir circle: `game_client.c`.
- Window dimensions and colours: `game_client.c`.
- Mistake limit: `GameLogic.c` in `POTION_TRACE_BAD` handler.

## Known first-prototype limitations

- Squares and basic rectangles instead of character art, animations and décor.
- No local audio, inventory popup, title menu or score screen beyond WIN/LOSE.
- No wall, player-to-player or cauldron collision yet: player motion is bounded
  to the rectangular room.
- The partner sees the tracing modal / wait indicator, **not** a live copy of
  the other player's stroke. Later, send stroke coordinates over DDS.
- Client disconnects and reconnects aren't handled; server has two slots and
  a full restart starts a new game.
- Drawing detection assumes the target is traced in its indicated order.
- Full client/server graphical integration **could not be run** here because
  the build environment did not have Cyclone DDS or GLFW development libraries.
  The platform-independent game logic and tracing tests passed, and all source
  files passed a C syntax check using lightweight API stubs.
