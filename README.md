# The Gingerbread Curse — Timed Recipes v2 (cooperative mode)

This version extends the **working bidirectional-tracing baseline**; the `Trace.c` and `Trace.h` files are byte-for-byte unchanged. The client/server threading model is unchanged: server main owns game rules, server DDS worker handles messages, and each client's GLFW main thread is separate from its DDS worker.

See **TIME_LIMITS.md** for countdown durations, timeout penalties, and testing.

## What's new

1. **Random recipes**: The server shuffles Moonflower, Ginger Root, Black Feather and Sugar Glass at the beginning of each recipe. The ingredient-specific rune stays the same regardless of the shuffled order: Moonflower = pentagram, Ginger Root = circle, Black Feather = flower, Sugar Glass = rectangle.
2. **Multiple recipes**: The game advances automatically to a new shuffled four-ingredient recipe after the last successful stir. Three fully completed recipes trigger a cooperative win. Change `RECIPES_TO_WIN` in `GameRules.h` (rebuild both executables) to change this target.
3. **Both players required**: Player 1 gathers the first ingredient, Player 2 gathers the second, and so on. The **other player** performs each ingredient's rune tracing. The **collector** then stirs. Both players must join, and both must participate in every stage. The server verifies the responsible player's ID and token before accepting any action.

The server **still ends the run if the wrong ingredient is added**, as in the working prototype. The timed version also counts unsuccessful traces and phase timeouts toward three mistakes per recipe; mistakes reset when the next recipe begins.

## Example round

| Step | Player 1 | Player 2 |
| --- | --- | --- |
| 1 | Collect/add ingredient | Trace rune |
| 1 (stir) | Stir the cauldron | Wait |
| 2 | Trace rune | Collect/add ingredient |
| 2 (stir) | Wait | Stir the cauldron |
| 3 | Collect/add ingredient | Trace rune |
| 4 | Trace rune | Collect/add ingredient |

Both clients see the same server-generated recipe, updated progress and current player role.

## Build on Linux

Install your existing Cyclone DDS, GLFW3 and OpenGL development dependencies. Copy the **same `stb_image.h` and `stb_easy_font.h` you used for the working game** into this directory.

```bash
chmod +x build.sh
./build.sh
```

The build script regenerates `messages.c` and `messages.h` using `idlc -l c messages.idl`, then builds server, client and tests. **Do not mix an old server executable with a new client**: the `GameState` DDS type changed.

Run in three terminals:

```bash
./server  # terminal 1
./client  # terminal 2
./client  # terminal 3
```

## Test recipe progression

1. Start server and two clients. The recipe panel should show a shuffled ingredient order, identical on both screens.
2. Player 1 collects **the first listed ingredient**, using E, and presses E by the cauldron.
3. Player 2 traces that ingredient's rune in either direction. Only their client should accept drawing.
4. Player 1 stirs the circle. The recipe stage advances.
5. Repeat with Player 2 collecting the second ingredient, Player 1 tracing, and Player 2 stirring.
6. After four ingredients, the recipe should automatically change; counter becomes `RECIPE 2 / 3` and the ingredient squares reappear.
7. Finish three complete recipes to get the cooperative victory.

The game can be tested on one laptop with two separate client windows; only the focused window receives key input.

## Architecture & safety

- `GameRules.c`: seeded Fisher-Yates shuffle, ingredient rules, role permissions, sequence progression (server-main-thread only).
- `messages.idl`: adds recipe order, round, completion count, used-ingredient mask to GameState.
- `server.c`: sends those new fields in a snapshot via existing DDS worker.
- `client.c`: renders shuffled recipe, hides used ingredients, selects rune based on ingredient ID, shows role prompts.
- `Trace.c` and `Trace.h`: **unchanged** tested bidirectional tracing.
- `ServerBridge.c` and `ServerBridge.h`: unchanged thread-safe handoff.
- `test_game.c`, `test_trace_integration.c`, `test_threads.c`, `test_recipe_preview.c`: offline tests.

## Limitations

- This implements **mandatory two-player co-op**, not a competitive race. Competitive mode would require independent player recipe states and scoring, and is better added as a selectable mode in a separate change.
- Joining does not currently include a disconnect/rejoin protocol; close the old processes and restart the server for a fresh session.
- I verified C syntax using interface stubs and ran headless logic/tracing/thread tests; a complete live DDS+OpenGL playtest still needs to happen on your laptop.
