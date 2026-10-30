# Timed Recipe Stages — v2

## Default limits

- **Collect and deliver ingredient:** 60 seconds
- **Trace the rune:** 40 seconds
- **Stir the cauldron:** 25 seconds

Edit `COLLECT_TIME_MS`, `TRACE_TIME_MS`, or `STIR_TIME_MS` in `GameRules.h` and rebuild both server/client.

## Gameplay behavior

- The timer starts **only after two players join**. Waiting for the second player is unlimited.
- A new countdown starts for each **collect**, **trace**, and **stir** phase.
- On timeout, the server adds one mistake and starts a new attempt with a fresh countdown; the ingredient is dropped on collect timeout.
- Any failed trace (even before the timer expires) also restarts the trace countdown. Stale client success messages from a timed-out/failed trace are rejected using the session ID.
- Three total mistakes in the current recipe cause `PHASE_LOSE`. Mistakes reset when the next recipe starts.
- The client displays a server-synchronised countdown and highlights the final ten seconds in red. The timer displayed is the latest server snapshot, updated approximately every 40ms. The actual deadline is always enforced by the server, not the client.
- Timers stop at WIN or LOSE. On completing a recipe, the next shuffled recipe starts with a full collect countdown.

## Threading

Server main thread owns `GameRules` and invokes `game_tick(&game, monotonic_ms)` before processing queued events. Only that thread handles timeouts. Server DDS thread publishes `GameState.int_timer_remaining_ms` and `int_timer_limit_ms`. Client DDS thread receives updates; the GLFW main thread displays them. Existing `Trace.c`, `Trace.h`, `ServerBridge.c`, and `ServerBridge.h` are unchanged.

## Build

```sh
chmod +x build.sh
./build.sh
./server
# Launch ./client in two other terminals.
```

Install/use the same `stb_image.h` and `stb_easy_font.h` as in your working project.
`messages.idl` changed, so rebuild both executables with the generated `messages.c`/`messages.h` rather than mixing versions.

Run `./test_timers` to check stage transition/timeouts independently of the DDS/GLFW windows.
