# The Gingerbread Curse — Recipe Variety + Reduced DDS Traffic (v3)

## What is new

- Ten available ingredients: Moonflower, Ginger Root, Black Feather, Sugar Glass, Witch Moss, Night Berry, Toadstool, Phoenix Ash, Silver Dew and Thornvine.
- Each recipe chooses **2, 3 or 4 distinct ingredients** from the pool of ten. The order also changes. Adjacent recipes cannot use the same unordered ingredient combination.
- The server generates and publishes recipes; clients display only the ingredients for the current recipe. `GameState` now carries `int_recipe_length` plus up to four ingredient IDs (unused slots are -1).
- A recipe automatically transitions to the next one after the last successful stir. The existing three-recipe victory condition remains.
- All ten ingredients appear as collectible coloured squares in two rows of five. Use `E` near an ingredient to collect, and `E` near the cauldron to add it. Incorrect ingredient -> loss, unchanged.
- Each ingredient is tied to one of the **existing** four rune shapes; `Trace.c` and `Trace.h` are unchanged. No new tracing algorithms.
- Cooperative roles still require both players for every ingredient (collector -> teammate traces -> collector stirs). Starting collector alternates across rounds.
- Timer limits remain at 60 seconds collect / 40 seconds trace / 25 seconds stir.

## Reduced network traffic

- Server no longer publishes player positions for stationary players every 40 ms. Position updates are sent at most every 100 ms when changing, with a 500 ms refresh if stationary.
- GameState now publishes immediately on game-state revision changes, approximately once per second for countdown updates, and at most every 1.5 seconds as a recovery refresh.
- Client sends movement inputs around 18 times per second instead of 36. Each step is now 7 px instead of 4 px, to preserve comparable movement speed.
- Server DDS worker polling sleeps 10 ms rather than 5 ms, and main thread handles up to 128 queued events per tick instead of 1024, reducing prolonged processing under load.
- The architecture remains exactly two threads per process: the server main thread owns the authoritative rules, server DDS worker handles networking; each client uses a GLFW main thread and DDS worker.

These changes **reduce unnecessary message volume** but do not guarantee that all stutters are fixed; validate on your own laptop and inspect the included heartbeat logs.

## Install the changed-file patch

1. **Back up your working game first.**
2. Extract `Gingerbread_Recipe_Variety_Performance_Patch.zip`.
3. Copy its `.c`, `.h`, and `messages.idl` files into your current game directory, overwriting matching names. You can copy the test sources too.
4. **Do not replace** `Trace.c`, `Trace.h`, `Render.c`, `Render.h`, `ServerBridge.c`, or `ServerBridge.h`; they are unchanged.
5. Keep your existing `build.sh`, `stb_image.h`, and `stb_easy_font.h`.
6. Shut down every running server/client. Run `./build.sh` which must regenerate `messages.c` and `messages.h` from the new `messages.idl` using `idlc -l c messages.idl`. Both server and clients must be rebuilt and restarted using the same IDL revision.
7. Launch one `./server` and two `./client` processes.

### Check randomization

At startup the server prints e.g. `[server main] Recipe 1 (3 ingredients): ...`. On every completed recipe it prints the next recipe and the selected ingredient count.

### Check traffic

The server logs cumulative `posTx` and `stateTx` every 2 seconds; compare increments between logs to the earlier build. Idle position updates should be around 2 per second *per player* rather than 25 per second; countdown state changes occur around 1 per second. Network delivery and movement responsiveness still depend on the machine and DDS setup.

### Build dependencies

Existing build script requires `idlc`, CycloneDDS development package, GLFW3, OpenGL libraries, and your `stb_image.h` / `stb_easy_font.h` headers.

### Checks completed in this environment

- Game-rule and timer tests, including varying lengths and three consecutive recipes.
- 500-seed recipe variety tests (2, 3, and 4 ingredients; all ten represented).
- Reverse tracing + stirring integration test.
- Thread-bridge stress test, 20,002 events.
- Server and client C syntax checks using interface stubs.

**Not tested here:** full Cyclone DDS/GLFW runtime; these libraries aren't installed in the evaluation environment.
