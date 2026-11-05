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

## Windows setup (WSL, Ubuntu 24.04)

The game is set up for Ubuntu 24.04 under WSL. On a new laptop, in an admin PowerShell:

```powershell
wsl --install -d Ubuntu-24.04
wsl --set-default Ubuntu-24.04
```

Restart if asked, open Ubuntu once to create your user, then double-click `run.bat`. It installs the packages on first run, builds, and starts the game.

To check the setup, run this inside Ubuntu (`run.bat` also prints it on every launch, with a warning if the default distro isn't 24.04):

```bash
lsb_release -ds; pkg-config --modversion CycloneDDS
```

Optional, for playing across two laptops: copy `.wslconfig` from this repo to `%UserProfile%\.wslconfig` (for example `C:\Users\<you>\.wslconfig`), then run `wsl --shutdown` in PowerShell. WSL only reads the file from there. It turns on mirrored networking so DDS discovery reaches other machines on the network.

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
- `Art.c` and `Art.h`: all game art, drawn from code (no image files). Static scenery is rendered once and cached in a texture; only animated parts are redrawn each frame.
- `Render.c` and `Render.h`: shape primitives and TTF text. The handwritten font is `assets/PatrickHand-Regular.ttf` (SIL Open Font License, see `assets/PatrickHand-OFL.txt`), baked by `stb_truetype.h`. **Run the client from the project folder** so it can find the font.
- `Accounts.c`, `AccountsMongo.c`: salted PBKDF2-SHA256 password hashing, plus the two account stores.
- `AuthWorker.c`: a dedicated server thread for logins, so slow hashing and database waits never stall the game or the DDS thread.
- `SecureDds.c`, `security/make_certs.sh`: DDS Security. All game traffic is authenticated and encrypted, and client certificates cannot publish server-only topics.
- `Trace.c` and `Trace.h`: **unchanged** tested bidirectional tracing.
- `ServerBridge.c` and `ServerBridge.h`: unchanged thread-safe handoff.
- `test_game.c`, `test_trace_integration.c`, `test_threads.c`, `test_recipe_preview.c`: offline tests.

## Accounts and the `.env` file

The game opens on the login page: log in, sign up, or play as a guest (a temporary server-issued name that isn't saved). The main menu (Play, Settings, Quit) comes next. `assets/login.png` and `assets/main_menu.png` are AI-generated concept art, so declare them with your AI use; the menu image was edited to remove its Login sign. Accounts are stored in MongoDB when a `.env` file containing `MONGODB_URI` is present, and otherwise in a local `accounts.db` file. The game plays the same either way. `.env` and `accounts.db` are git-ignored and must not be committed or submitted. `security/generated/` is committed on purpose: every teammate needs the same CA and identities, or their server and clients can't authenticate each other. Only regenerate it (`bash security/make_certs.sh`) if you then commit the new files for everyone, and keep the repo private, because it holds the private keys. If MongoDB can't be reached, the server prints a warning and uses the local file.

### Leaving, rejoining and logging out

- **You stay logged in** until you press **Log out** (top-left of the main menu) or close the app. Guests see **Log in** there instead, which goes to the login page.
- **An account player who leaves or loses connection** (5 s without a heartbeat) keeps their seat. The round and timer pause, and their partner sees "Waiting for *name* to log back in and rejoin...". When they log in with the same account and press **Play**, they get the same seat, position and held ingredient back, and the round carries on.
- **A guest who leaves can't rejoin**, because a guest has no account to prove who they are. Their partner is told to return to the menu.
- **The game officially ends when both players have left**, and the server starts a fresh game.
- Server side: `game_leave` and `game_join_named` in `GameRules.c`; `GameState.int_away_player` tells clients whose seat is saved (0 means the leaver was a guest). `ACTION_LEAVE` leaves the game and keeps the login; `ACTION_LOGOUT` also ends the login session. Covered by the rejoin tests in `test_game.c`.

In the game, press Enter to chat and Esc to stop. The server verifies the sender, limits the rate and relays the message.

## Limitations

- This implements **mandatory two-player co-op**, not a competitive race. Competitive mode would require independent player recipe states and scoring, and is better added as a selectable mode in a separate change.
- Rejoining only works while the partner is still in the game, and only for account players. Guests, and saved seats, don't survive a server restart, and there are no separate rooms: the server runs one game at a time.
- DDS Security keeps game data, logins and chat private, but by design the discovery announcement shows the machine name and the handshake shows the permissions file (topic names).
- I verified C syntax using interface stubs and ran headless logic/tracing/thread tests; a complete live DDS+OpenGL playtest still needs to happen on your laptop.
