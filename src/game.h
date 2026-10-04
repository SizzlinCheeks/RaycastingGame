#ifndef GAME_H
#define GAME_H

#define MAX_PLAYERS 2
#define MAX_ZOMBIES 24          // most zombies alive at once

#define PLAYER_RADIUS   8.0f
#define ZOMBIE_RADIUS   8.0f
#define PLAYER_SPEED    80.0f   // world units per second
#define SPRINT_MULT     1.6f
#define TURN_SPEED      120.0f  // degrees per second
#define PLAYER_MAX_HP   100

#define MAG_SIZE        12
#define MAX_RESERVE     240
#define FIRE_DELAY      0.22f   // seconds between shots
#define RELOAD_TIME     1.2f
#define GUN_DAMAGE      50

// Sprite indices (assets/sprites/enemyN.png)
#define SPR_RED      0
#define SPR_ZOMBIE   1
#define SPR_PLAYER   2   // blue guy = your teammate
#define SPR_GNOME    3
#define NUM_SPRITES  4

enum { STATE_PLAYING, STATE_GAMEOVER };

typedef struct {
    int   active;       // is someone controlling this player?
    int   alive;
    float x, y, a;      // position (world units) and angle (degrees)
    int   health;
    int   points;
    int   kills;
    float lastHurt;     // host only: time of last hit, for health regen
    float regenAcc;     // host only
    unsigned int shotsHandled; // host only: shot counter from the network
} Player;

typedef struct {
    int   active;
    float x, y;
    float tx, ty;       // client only: latest network position, we glide toward it
    int   hp;
    int   sprite;
    float attackCooldown;
    float hurtFlash;
} Zombie;

typedef struct {
    Player players[MAX_PLAYERS];
    int    localPlayer;          // which player this machine controls
    Zombie zombies[MAX_ZOMBIES];
    int    state;
    int    round;
    int    zombiesToSpawn;       // still waiting to spawn this round
    float  spawnTimer;
    float  roundBreak;           // pause between rounds
    float  time;                 // seconds since start
    unsigned int restartCount;   // bumps every restart, so clients can reset too

    // Local-only weapon state (each machine handles its own ammo)
    int    mag, reserve;
    float  fireCooldown, reloadTimer;
    float  muzzleFlash, hurtFlash, hitMarker;
    unsigned int shotCounter;    // total shots fired by the local player
} Game;

void game_init(Game *g);
void game_restart(Game *g);
void game_spawn_player(Game *g, int idx);
void game_reset_weapon(Game *g);

// Host / single player only: zombies, rounds, damage.
void game_update(Game *g, float dt);

// Hit scan for player idx. Returns 0 = miss, 1 = hit, 2 = kill.
int game_fire(Game *g, int idx);

int game_zombies_remaining(const Game *g);

#endif
