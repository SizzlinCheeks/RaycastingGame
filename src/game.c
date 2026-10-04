#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "game.h"
#include "map.h"
#include "utils.h"

// Open cells (x, y) where zombies crawl out
static const int spawnCells[][2] = {
    {1, 1}, {18, 1}, {10, 1}, {1, 9}, {18, 9}, {10, 8},
    {1, 18}, {18, 18}, {4, 12}, {15, 3},
};
#define NUM_SPAWNS (int)(sizeof(spawnCells) / sizeof(spawnCells[0]))

// Player start cells, one per player
static const int playerStarts[MAX_PLAYERS][2] = { {10, 17}, {12, 17} };

// Distance (in cells) from every cell to the nearest living player.
// Zombies walk "downhill" on this field, so they path around walls.
static int flow[mapY][mapX];

static float cellCenter(int c) { return (c + 0.5f) * mapS; }

void game_reset_weapon(Game *g)
{
    g->mag = MAG_SIZE;
    g->reserve = MAX_RESERVE;
    g->fireCooldown = 0;
    g->reloadTimer = 0;
}

void game_spawn_player(Game *g, int idx)
{
    Player *p = &g->players[idx];
    p->alive = 1;
    p->health = PLAYER_MAX_HP;
    p->x = cellCenter(playerStarts[idx][0]);
    p->y = cellCenter(playerStarts[idx][1]);
    p->a = 90; // facing "up" the map
    p->lastHurt = -100;
    p->regenAcc = 0;
}

void game_restart(Game *g)
{
    memset(g->zombies, 0, sizeof(g->zombies));
    g->state = STATE_PLAYING;
    g->round = 0;
    g->zombiesToSpawn = 0;
    g->spawnTimer = 0;
    g->roundBreak = 2.0f;
    g->restartCount++;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        p->points = 500;
        p->kills = 0;
        game_spawn_player(g, i);
    }
    game_reset_weapon(g);
    g->muzzleFlash = g->hurtFlash = g->hitMarker = 0;
}

void game_init(Game *g)
{
    memset(g, 0, sizeof(*g));
    g->players[0].active = 1;
    game_restart(g);
}

int game_zombies_remaining(const Game *g)
{
    int n = g->zombiesToSpawn;
    for (int i = 0; i < MAX_ZOMBIES; i++)
        if (g->zombies[i].active) n++;
    return n;
}

static int activePlayers(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) if (g->players[i].active) n++;
    return n;
}

static void buildFlowField(const Game *g)
{
    static int queue[mapX * mapY][2];
    int head = 0, tail = 0;

    for (int y = 0; y < mapY; y++)
        for (int x = 0; x < mapX; x++)
            flow[y][x] = 9999;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Player *p = &g->players[i];
        if (!p->active || !p->alive) continue;
        int cx = (int)(p->x / mapS), cy = (int)(p->y / mapS);
        if (mapCellIsWall(cx, cy) || flow[cy][cx] == 0) continue;
        flow[cy][cx] = 0;
        queue[tail][0] = cx; queue[tail][1] = cy; tail++;
    }

    static const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    while (head < tail) {
        int cx = queue[head][0], cy = queue[head][1]; head++;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
            if (mapCellIsWall(nx, ny) || flow[ny][nx] <= flow[cy][cx] + 1) continue;
            flow[ny][nx] = flow[cy][cx] + 1;
            queue[tail][0] = nx; queue[tail][1] = ny; tail++;
        }
    }
}

static Player *nearestPlayer(Game *g, float x, float y, float *outDist)
{
    Player *best = NULL;
    float bestD = 1e30f;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        if (!p->active || !p->alive) continue;
        float d = hypotf(p->x - x, p->y - y);
        if (d < bestD) { bestD = d; best = p; }
    }
    if (outDist) *outDist = bestD;
    return best;
}

static void spawnZombie(Game *g)
{
    int slot = -1;
    for (int i = 0; i < MAX_ZOMBIES; i++)
        if (!g->zombies[i].active) { slot = i; break; }
    if (slot < 0) return;

    // Prefer a spawn point that is not right next to a player
    int pick = rand() % NUM_SPAWNS;
    for (int tries = 0; tries < 20; tries++) {
        int s = rand() % NUM_SPAWNS;
        float d;
        nearestPlayer(g, cellCenter(spawnCells[s][0]), cellCenter(spawnCells[s][1]), &d);
        if (d > 6 * mapS) { pick = s; break; }
    }

    static const int zombieSprites[] = { SPR_RED, SPR_ZOMBIE, SPR_ZOMBIE, SPR_GNOME };
    Zombie *z = &g->zombies[slot];
    memset(z, 0, sizeof(*z));
    z->active = 1;
    z->x = z->tx = cellCenter(spawnCells[pick][0]) + randf(-4, 4);
    z->y = z->ty = cellCenter(spawnCells[pick][1]) + randf(-4, 4);
    z->hp = 100 + 40 * (g->round - 1);
    z->sprite = zombieSprites[rand() % 4];
    z->attackCooldown = 0.5f;
    g->zombiesToSpawn--;
}

static void startRound(Game *g)
{
    g->round++;
    g->zombiesToSpawn = 5 + 3 * g->round + (activePlayers(g) - 1) * 2 * g->round;
    g->spawnTimer = 0;
    // Anyone who went down comes back for the new round
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        if (p->active && !p->alive) { p->alive = 1; p->health = PLAYER_MAX_HP; }
    }
}

static void moveZombie(Game *g, Zombie *z, float dt)
{
    float dist;
    Player *target = nearestPlayer(g, z->x, z->y, &dist);
    if (!target) return;

    z->attackCooldown -= dt;
    if (dist < PLAYER_RADIUS + ZOMBIE_RADIUS + 6) {
        if (z->attackCooldown <= 0) {
            target->health -= 34;
            target->lastHurt = g->time;
            z->attackCooldown = 1.0f;
            if (target->health <= 0) { target->health = 0; target->alive = 0; }
        }
        return;
    }

    // Pick where to walk: straight at the player if we can see them,
    // otherwise to the neighbouring cell that is closest along the flow field.
    float goalX = target->x, goalY = target->y;
    int cx = (int)(z->x / mapS), cy = (int)(z->y / mapS);
    float dx = target->x - z->x, dy = target->y - z->y;
    int canSee = mapRaycast(z->x, z->y, dx / dist, dy / dist, NULL, NULL) > dist;
    if (!canSee && !mapCellIsWall(cx, cy)) {
        int best = flow[cy][cx];
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                int nx = cx + ox, ny = cy + oy;
                if ((!ox && !oy) || mapCellIsWall(nx, ny)) continue;
                // no cutting diagonally through wall corners
                if (ox && oy && (mapCellIsWall(cx + ox, cy) || mapCellIsWall(cx, cy + oy))) continue;
                if (flow[ny][nx] < best) {
                    best = flow[ny][nx];
                    goalX = cellCenter(nx);
                    goalY = cellCenter(ny);
                }
            }
        }
    }

    dx = goalX - z->x; dy = goalY - z->y;
    float len = hypotf(dx, dy);
    if (len < 0.001f) return;
    float speed = fminf(35.0f + 5.0f * g->round, 95.0f);
    mapMove(&z->x, &z->y, dx / len * speed * dt, dy / len * speed * dt, ZOMBIE_RADIUS);
}

void game_update(Game *g, float dt)
{
    g->time += dt;
    if (g->state != STATE_PLAYING) return;

    // Rounds
    if (g->roundBreak > 0) {
        g->roundBreak -= dt;
        if (g->roundBreak <= 0) startRound(g);
    } else {
        g->spawnTimer -= dt;
        if (g->zombiesToSpawn > 0 && g->spawnTimer <= 0) {
            spawnZombie(g);
            g->spawnTimer = fmaxf(0.4f, 2.0f - g->round * 0.1f);
        }
        if (game_zombies_remaining(g) == 0) g->roundBreak = 6.0f;
    }

    // Zombies
    buildFlowField(g);
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g->zombies[i];
        if (!z->active) continue;
        if (z->hurtFlash > 0) z->hurtFlash -= dt;
        moveZombie(g, z, dt);
    }

    // Keep zombies from stacking on top of each other
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *a = &g->zombies[i];
        if (!a->active) continue;
        for (int j = i + 1; j < MAX_ZOMBIES; j++) {
            Zombie *b = &g->zombies[j];
            if (!b->active) continue;
            float dx = b->x - a->x, dy = b->y - a->y;
            float d = hypotf(dx, dy);
            if (d < 14.0f && d > 0.001f) {
                float push = (14.0f - d) * 0.5f;
                mapMove(&a->x, &a->y, -dx / d * push, -dy / d * push, ZOMBIE_RADIUS);
                mapMove(&b->x, &b->y,  dx / d * push,  dy / d * push, ZOMBIE_RADIUS);
            }
        }
    }

    // Players: regenerate health, check for game over
    int anyAlive = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        if (!p->active || !p->alive) continue;
        anyAlive = 1;
        if (p->health < PLAYER_MAX_HP && g->time - p->lastHurt > 4.0f) {
            p->regenAcc += 50.0f * dt;
            int add = (int)p->regenAcc;
            p->regenAcc -= add;
            p->health = p->health + add > PLAYER_MAX_HP ? PLAYER_MAX_HP : p->health + add;
        }
    }
    if (!anyAlive) g->state = STATE_GAMEOVER;
}

int game_fire(Game *g, int idx)
{
    Player *p = &g->players[idx];
    if (!p->active || !p->alive || g->state != STATE_PLAYING) return 0;

    float dirX = cosf(degToRad(p->a)), dirY = -sinf(degToRad(p->a));
    float rightX = -dirY, rightY = dirX;
    float wallDist = mapRaycast(p->x, p->y, dirX, dirY, NULL, NULL);

    Zombie *hit = NULL;
    float hitDepth = wallDist;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g->zombies[i];
        if (!z->active) continue;
        float rx = z->x - p->x, ry = z->y - p->y;
        float depth = rx * dirX + ry * dirY;
        float side = rx * rightX + ry * rightY;
        if (depth > 0 && depth < hitDepth && fabsf(side) < ZOMBIE_RADIUS + 1) {
            hit = z;
            hitDepth = depth;
        }
    }
    if (!hit) return 0;

    hit->hp -= GUN_DAMAGE;
    hit->hurtFlash = 0.15f;
    p->points += 10;
    if (hit->hp <= 0) {
        hit->active = 0;
        p->points += 60;
        p->kills++;
        return 2;
    }
    return 1;
}
