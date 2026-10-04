# 8-Bit Zombies

A Call of Duty Zombies–style survival game drawn with an old-school raycaster
(like Wolfenstein 3D), in C with GLUT/OpenGL and SDL2. You can play solo or
with a friend over the network.

## Play in the browser

`web/index.html` is a single-file JavaScript version of the game (solo only). Open it in any browser, no build needed.

The browser version has a bigger 40×30 map split into six areas. You buy doors to open new areas, and you buy weapons
chalked on the walls (M14, Olympia, MP40, STG-44, Trench Gun, Ray Gun). Look at a door or a chalk drawing and press **F**.
You carry two weapons, and **1 / 2** or the mouse wheel switches between them.

## Build

**macOS** (Homebrew):

```sh
brew install pkg-config sdl2 sdl2_image sdl2_mixer
make
```

**Linux** (Ubuntu/Debian):

```sh
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev freeglut3-dev
make
```

Run it from the project folder so it can find `assets/`.

## Play

```sh
./zombies                    # solo
./zombies host               # host a co-op game on UDP port 7777
./zombies join 192.168.1.20  # join your friend (use the host's IP)
```

| Key              | Action            |
|------------------|-------------------|
| W / S, ↑ / ↓     | Move              |
| A / D, ← / →     | Turn              |
| Q / E            | Strafe            |
| M (hold)         | Sprint            |
| Space / click    | Shoot             |
| R                | Reload            |
| Enter            | Restart after game over (host or solo) |
| X / Esc          | Quit              |

Zombies come in rounds. Each round brings more zombies, and they are tougher
and faster. You get 10 points for a hit and 60 for a kill. Your ammo is
refilled at the start of every round. If you go down in co-op, you come back
next round as long as your friend survives.

## Playing with a friend

The host runs the actual game (zombies, rounds, damage) and sends the world to
the client 20 times a second over UDP. The client moves its own player and
tells the host where it is and when it shoots.

* **Same Wi-Fi / LAN:** run `./zombies host` on one machine and
  `./zombies join <host-ip>` on the other. On a Mac, you can find the IP in
  System Settings → Wi-Fi → Details, or with `ipconfig getifaddr en0`.
  If macOS asks whether to allow incoming connections, choose Allow.
* **Different houses/networks:** the easiest option is a free virtual LAN such
  as [Tailscale](https://tailscale.com) or ZeroTier. Both of you install it,
  then join using the host's Tailscale IP. The other option is to forward
  UDP port 7777 on the host's router to the host's machine.
* To use a different port: `./zombies host 9000` and `./zombies join <ip> 9000`.

## Code layout

| File            | What it does |
|-----------------|--------------|
| `src/main.c`    | Window, input, game loop, weapon handling |
| `src/game.c`    | Rounds, zombie spawning, zombie AI (path-finding flow field), shooting, damage |
| `src/map.c`     | The level, wall collision, DDA ray casting |
| `src/render.c`  | 3D raycast view, sprites, pistol, minimap, HUD |
| `src/net.c`     | UDP co-op networking |
| `src/audio.c`   | Optional music (`assets/music/background.mp3`) plus generated 8-bit sound effects |

## Sprites

`assets/sprites/enemy1.png` to `enemy4.png` are 32×32 RGBA PNGs. Red, green
and the gnome are zombies, and the blue guy is your co-op partner. Swap them
for new art at the same size, using transparency for the background.

## What was broken in the original code

* The Makefile compiled `new.c`, but the file is `main.c`. It also used the
  pkg-config names `sdl2_mixer`/`sdl2_ttf`, which are case-sensitive and should
  be `SDL2_mixer`. `utils.h`/`utils.c` were missing.
* `map` was defined in both `main.c` and `map.c`, which causes a duplicate
  symbol link error.
* The enemy PNG went straight to OpenGL in whatever format SDL loaded it. A
  palette or RGB image becomes garbage, and a missing file crashed the game
  (`surface->format` on NULL). Images are now converted to RGBA first, and a
  placeholder is used if a file is missing.
* Enemies started inside walls (`100,100`) or outside the map (`700,100`),
  which read past the end of the `map` array.
* For rays pointing straight along an axis, the ray loop kept stepping with
  stale offsets (`dof = 8` but the loop ran to 20).
* Sprites used the wrong projection (480 instead of the real focal length),
  were 4px out of line with the walls (526 vs 530), weren't sorted by
  distance, and had no transparency.
* `SDL_INIT_VIDEO` was started alongside GLUT, and the two fight over the
  window on macOS. Only SDL audio is started now.
* A missing `background.mp3` called `exit(1)`. Music is optional now.
