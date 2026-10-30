# Gingerbread Curse — integrated bidirectional tracing

This folder is the *Freeze Diagnostics Fix* multiplayer client/server (the version that ran more smoothly with two clients), with the tested **bidirectional** mouse tracer integrated. Both clients and the server still use the slide-aligned threading model. No networking protocol or server game rules were changed.

## Full updated version

1. Extract the package.
2. Put `stb_easy_font.h` and `stb_image.h` in this folder (use the copies from your existing working game).
3. Make sure `idlc`, `pkg-config`, GCC, CycloneDDS, GLFW3 and OpenGL dev files are installed.
4. Run:

```bash
chmod +x build.sh
./build.sh
```

5. Stop existing game processes and start in separate terminals:

```bash
./server
./client
./client
```

The current `GameRules.c` still requires **two joined clients** before collecting and adding ingredients; a separate solo tracing debugger can be used to practise without DDS.

## Game flow

- Walk near Moonflower, press E to collect it, walk to the cauldron, press E.
- The server sends `GameState` with `PHASE_TRACE`. Only the current actor can trace.
- Client main thread creates the correct target (pentagram, circle, flower, rectangle).
- Start at the **green dot** and drag along the dots in **either direction**.
- Client main thread checks success; it queues `ACTION_TRACE_OK` or `ACTION_TRACE_BAD` for the DDS worker.
- DDS worker publishes `GameAction`; server main thread changes the authoritative game phase.
- A successful rune switches to `PHASE_STIR`. Trace the new circle in either direction to finish the stage.
- Other players see the authoritative phase change via `GameState`. Mouse tracing happens locally; only results are sent to the server.

