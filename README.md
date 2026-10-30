# The Gingerbread Curse — slide-aligned threading edition (pure C)

This edition keeps the **original four DDS topics** (`JoinRequest`, `JoinResponse`, `Input`, `Position`), with the game message extensions from the previous prototype. It follows the three uploaded lecture slides' thread-ownership model exactly.

## Thread ownership

| Process | Main thread | DDS worker thread |
| --- | --- | --- |
| Server | Sole owner and writer of authoritative `GameRules`; processes joins, player movement, collisions, interactions, recipe state and outcomes | Sole owner of Cyclone DDS entities; receives join/input/action events and publishes response, player positions and game-state messages |
| Client 1 | GLFW event polling, keyboard/mouse input, tracing, OpenGL rendering | Sole owner of client DDS entities; sends join/inputs/actions, receives positions and state |
| Client 2 | Same as Client 1 | Same as Client 1 |

The **server main thread runs at ~60 Hz**; the DDS worker independently polls network data every ~5 ms, and publishes snapshots around every 40 ms or after game-state revisions.

### Server synchronization

`ServerBridge.c` / `ServerBridge.h` implements three mutex-protected handoffs:

1. **Event queue (DDS -> main):** join requests, player movement and potion actions. The DDS worker doesn't update `GameRules` directly.
2. **Join-reply queue (main -> DDS):** main assigns a player ID and queues a response, which the DDS worker publishes. Join requests are correlated by unique client token.
3. **Published snapshot (main -> DDS):** main copies its authoritative `GameRules` into the bridge under the mutex. The worker copies it out under the mutex, releases the lock, then publishes to DDS. No DDS operation runs with the mutex locked.

The queue is bounded; overflows produce an error message and drop incoming events rather than corrupting memory. For gameplay across untrusted networks, add stronger handling for dropped actions and authenticated sessions later.

### Client synchronization

The client's existing `NetworkBridge` mutex protects player ID, positions, current game phase, outgoing directions/actions, and stop/error flags. The main GLFW thread copies the bridge state at the beginning of each frame, **then releases the mutex before rendering**. All OpenGL calls and trace mouse callbacks remain on the main thread. The DDS worker never calls OpenGL or GLFW.

The client deliberately doesn't free/reuse player slots on disconnect, consistent with the original lecture example. Each client can render only players for which it has received valid position messages. The server is the sole source of correct positions.

## Files

- `server.c` — server main game loop + DDS worker
- `ServerBridge.c` / `ServerBridge.h` — thread-safe server queues and snapshot exchange
- `client.c` — client main GLFW/OpenGL loop + DDS worker
- `messages.idl` — extended original DDS IDL
- `GameRules.c` / `.h` — original-style authoritative movement, collision and gameplay
- `Render.c` / `.h`, `Trace.c` / `.h` — rendering and rune tracing
- `test_threads.c` — concurrent DDS-producer simulation + main game owner + snapshot reader
- `test_game.c` — game and tracing tests
- `original_backup/` — the original provided demo client, server, and IDL

## Build

Ensure GCC, `idlc` (Cyclone DDS), GLFW and OpenGL development libraries are installed. Copy `stb_easy_font.h` and `stb_image.h` from your working project into this folder.

```bash
chmod +x build.sh debug_build.sh
./build.sh
```

The build regenerates `messages.c` and `messages.h` with `idlc -l c messages.idl`; **don't reuse generated files from a different version of messages.idl**.

## Run (three terminals, same laptop)

Stop any older game servers/clients before proceeding.

```bash
# Terminal 1
./server
# Terminal 2
./client
# Terminal 3
./client
```

The server prints `[server main] Game state loop running` and `[server DDS worker] running`. Each client should join with a distinct player ID. WASD/arrow keys move, E interacts, Q drops, and the mouse traces the highlighted shape.

## Debug a crash

```bash
./debug_build.sh
ASAN_OPTIONS=detect_leaks=0 ./server 2>&1 | tee server.log
ASAN_OPTIONS=detect_leaks=0 ./client 2>&1 | tee client1.log
ASAN_OPTIONS=detect_leaks=0 ./client 2>&1 | tee client2.log
```

If crashes persist, copy the first AddressSanitizer stack trace or use `gdb --args ./server` / `gdb --args ./client` and `bt` to identify the fault. A crash immediately after Client 2 joins can also be caused by unexpected DDS samples, stale generated IDL, or an out-of-range player ID—not just a threading problem.

## Validation limits

The standalone game, mouse-path and bridge concurrency tests can run without DDS or GLFW installed. A full three-process live DDS + OpenGL multiplayer test must still be performed on your own machine because those development libraries and a GUI display are not present in this environment.


## Diagnosing an apparent freeze (this patch)

This revision preserves the lecture architecture: server main is the only game-state
writer, each process has one DDS worker, and client OpenGL stays on the main thread.
It **does not claim to have proven the cause** of a freeze on your laptop.

Changes:
- Explicit **best-effort / keep-last(8)** QoS on frequent `Input`, `Position`, and
  `GameState` topics. This reduces backpressure and stale queues for fast-changing data.
- Explicit **reliable / keep-last(16), max-blocking 100ms** QoS on
  `JoinRequest`, `JoinResponse`, and `GameAction` topics so joining and actions
  remain reliable, without unbounded writer-history blocking.
- GLFW VSync disabled for multi-window debugging; main thread sleeps 16ms/frame
  instead of busy-looping.
- Every 2 seconds, server and client print `[MAIN] alive` and `[DDS] alive`
  counters. `SLOW FRAME` is printed when poll/draw/swap exceeds 200ms.
- No DDS or OpenGL call is made while holding bridge mutexes.

**Start fresh:** Stop old processes; rebuild both binaries using this copy of
`messages.idl` and generated `messages.c/.h`; start 1 server then 2 clients.
Capture separate logs (`stdbuf -oL -eL` may help):

```sh
./server 2>&1 | tee server.log
./client 2>&1 | tee client1.log
./client 2>&1 | tee client2.log
```

When frozen, leave the processes running and inspect the last lines:
- Server MAIN stops; DDS continues: main game loop blocked, investigate main thread.
- Server DDS stops; MAIN continues: DDS read/write/transport stall or worker failure.
- Client MAIN stops; client DDS continues: GLFW/OpenGL frame loop blocked.
- Client DDS stops; client MAIN continues: DDS communication stalled.
- Both continue but no player movement: game state, join/identity or input logic.

For a blocked process, use `gdb -p <PID>` from another terminal, then run:
`thread apply all bt` and `detach`, `quit`. On Linux you may need debugger
permissions (ptrace access). Report the **actual thread stacks** to distinguish
mutex waits, DDS transport, and OpenGL driver stalls.

**Note:** Best-effort streams can lose individual movement or position updates,
which is acceptable for the present prototype because positions are republished.
For production gameplay, design reliable/recoverable event handling and snapshots.
Do not mix this build with old clients or servers that use different topic QoS.
