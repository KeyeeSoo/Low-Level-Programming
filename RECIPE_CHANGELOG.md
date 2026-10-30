# Recipe gameplay changes (from working baseline)

- `GameRules.h` and `.c`: server-only recipe shuffling, 3 consecutive recipes, alternating collector and tracer/stir roles, all stages require both players.
- `messages.idl`: extra GameState fields for recipe order, round, completed count, used ingredients.
- `server.c`: broadcasts these fields.
- `client.c`: renders random recipe, handles ingredient-specific runes, and shows cooperative roles.
- `test_game.c` and `test_trace_integration.c`: tests updated for new flow.
- `test_recipe_preview.c`: extra shuffle validity test.
- `build.sh`: runs the additional test.

`Trace.c`, `Trace.h`, `Render.c`, `Render.h`, `ServerBridge.c`, `ServerBridge.h` are unchanged from the working baseline.

This is a separate version: rebuild both binaries and regenerate the generated DDS `messages.c` and `messages.h`.
