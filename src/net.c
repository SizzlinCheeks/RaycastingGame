#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "net.h"

#define MAGIC        0x5A52  // "ZR"
#define VERSION      1
#define PKT_CLIENT   1       // client -> host: my position + shot counter
#define PKT_SNAPSHOT 2       // host -> client: everything in the world
#define TIMEOUT      5.0f
#define SNAPSHOT_RATE (1.0f / 20.0f)
#define CLIENT_RATE   (1.0f / 30.0f)

static int  sock = -1;
static int  mode = NET_SOLO;
static int  havePeer = 0;
static struct sockaddr_in peer;
static float clockTime = 0, lastRecv = -100, sendTimer = 0;
static uint32_t sendSeq = 0, lastSeqSeen = 0;
static char statusText[160] = "Solo";

// ---------------------------------------------------------------------------
// Tiny byte writer / reader so packets look the same on every machine
// (no struct padding or endianness surprises).
typedef struct { uint8_t data[1200]; int pos; int bad; } Buf;

static void putU8(Buf *b, uint32_t v) { if (b->pos + 1 > (int)sizeof(b->data)) { b->bad = 1; return; } b->data[b->pos++] = (uint8_t)v; }
static void putU16(Buf *b, uint32_t v) { putU8(b, v >> 8); putU8(b, v); }
static void putU32(Buf *b, uint32_t v) { putU16(b, v >> 16); putU16(b, v); }
static void putF32(Buf *b, float f) { uint32_t v; memcpy(&v, &f, 4); putU32(b, v); }

static uint32_t getU8(Buf *b) { if (b->pos + 1 > (int)sizeof(b->data)) { b->bad = 1; return 0; } return b->data[b->pos++]; }
static uint32_t getU16(Buf *b) { uint32_t hi = getU8(b); return (hi << 8) | getU8(b); }
static uint32_t getU32(Buf *b) { uint32_t hi = getU16(b); return (hi << 16) | getU16(b); }
static float getF32(Buf *b) { uint32_t v = getU32(b); float f; memcpy(&f, &v, 4); return f; }

static void putHeader(Buf *b, int type) { putU16(b, MAGIC); putU8(b, VERSION); putU8(b, type); }

// ---------------------------------------------------------------------------
static int openSocket(int port)
{
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 0; }
    if (port) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((uint16_t)port);
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(sock); sock = -1;
            return 0;
        }
    }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    return 1;
}

int net_host(int port)
{
    if (!openSocket(port)) return 0;
    mode = NET_HOST;
    snprintf(statusText, sizeof(statusText), "HOST on port %d - waiting for a friend", port);
    printf("Hosting on UDP port %d. Your friend runs: ./zombies join <your-ip> %d\n", port, port);
    return 1;
}

int net_join(const char *address, int port)
{
    struct addrinfo hints, *res;
    char portStr[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(portStr, sizeof(portStr), "%d", port);
    if (getaddrinfo(address, portStr, &hints, &res) != 0 || !res) {
        printf("Could not resolve host '%s'\n", address);
        return 0;
    }
    memcpy(&peer, res->ai_addr, sizeof(peer));
    freeaddrinfo(res);
    if (!openSocket(0)) return 0;
    mode = NET_CLIENT;
    havePeer = 1;
    snprintf(statusText, sizeof(statusText), "Connecting to %s:%d ...", address, port);
    printf("Joining %s:%d\n", address, port);
    return 1;
}

void net_shutdown(void)
{
    if (sock >= 0) close(sock);
    sock = -1;
    mode = NET_SOLO;
}

int net_mode(void) { return mode; }
int net_connected(void) { return havePeer && clockTime - lastRecv < TIMEOUT; }
const char *net_status(void) { return statusText; }

static void sendBuf(Buf *b)
{
    if (b->bad) return;
    sendto(sock, b->data, b->pos, 0, (struct sockaddr *)&peer, sizeof(peer));
}

// ---------------------------------------------------------------------------
// Host side
static void hostSendSnapshot(Game *g)
{
    Buf b = {0};
    putHeader(&b, PKT_SNAPSHOT);
    putU32(&b, ++sendSeq);
    putU32(&b, g->restartCount);
    putU8(&b, g->state);
    putU16(&b, g->round);
    putU16(&b, g->zombiesToSpawn);
    putF32(&b, g->roundBreak);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        putU8(&b, p->active);
        putU8(&b, p->alive);
        putF32(&b, p->x); putF32(&b, p->y); putF32(&b, p->a);
        putU8(&b, p->health);
        putU32(&b, (uint32_t)p->points);
        putU16(&b, p->kills);
    }
    int count = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) if (g->zombies[i].active) count++;
    putU8(&b, count);
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g->zombies[i];
        if (!z->active) continue;
        putU8(&b, i);
        putF32(&b, z->x); putF32(&b, z->y);
        putU8(&b, z->sprite);
        putU8(&b, z->hurtFlash > 0);
    }
    sendBuf(&b);
}

static void hostHandleClient(Game *g, Buf *b, struct sockaddr_in *from)
{
    uint32_t restart = getU32(b);
    float x = getF32(b), y = getF32(b), a = getF32(b);
    uint32_t shots = getU32(b);
    if (b->bad) return;

    Player *p = &g->players[1];
    int samePeer = havePeer && from->sin_addr.s_addr == peer.sin_addr.s_addr && from->sin_port == peer.sin_port;
    if (!samePeer) {
        if (net_connected()) return; // someone is already playing
        peer = *from;
        havePeer = 1;
        p->active = 1;
        p->points = 500;
        p->kills = 0;
        game_spawn_player(g, 1);
        p->shotsHandled = shots;
        char ip[64];
        inet_ntop(AF_INET, &from->sin_addr, ip, sizeof(ip));
        snprintf(statusText, sizeof(statusText), "HOST - friend connected from %s", ip);
        printf("Friend joined from %s\n", ip);
    }
    lastRecv = clockTime;

    if (restart != g->restartCount) { p->shotsHandled = shots; return; } // client hasn't caught up yet
    if (p->alive) { p->x = x; p->y = y; p->a = a; }

    // Replay any shots we have not processed yet (counter survives packet loss)
    int pending = (int)(shots - p->shotsHandled);
    if (pending > 5) pending = 5;
    for (int i = 0; i < pending; i++) game_fire(g, 1);
    p->shotsHandled = shots;
}

// ---------------------------------------------------------------------------
// Client side
static void clientSendState(Game *g)
{
    Player *p = &g->players[g->localPlayer];
    Buf b = {0};
    putHeader(&b, PKT_CLIENT);
    putU32(&b, g->restartCount);
    putF32(&b, p->x); putF32(&b, p->y); putF32(&b, p->a);
    putU32(&b, g->shotCounter);
    sendBuf(&b);
}

static void clientHandleSnapshot(Game *g, Buf *b)
{
    uint32_t seq = getU32(b);
    if (seq <= lastSeqSeen) return; // old or duplicate packet
    lastSeqSeen = seq;

    uint32_t restart = getU32(b);
    int state = getU8(b);
    int round = getU16(b);
    int toSpawn = getU16(b);
    float roundBreak = getF32(b);
    struct { int active, alive, health, points, kills; float x, y, a; } pl[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        pl[i].active = getU8(b);
        pl[i].alive = getU8(b);
        pl[i].x = getF32(b); pl[i].y = getF32(b); pl[i].a = getF32(b);
        pl[i].health = getU8(b);
        pl[i].points = (int)getU32(b);
        pl[i].kills = getU16(b);
    }
    int count = getU8(b);
    if (b->bad) return;

    if (!net_connected() || restart != g->restartCount) {
        if (restart != g->restartCount) {
            // Host started a new game: reset our own player too
            game_spawn_player(g, g->localPlayer);
            game_reset_weapon(g);
        }
        g->restartCount = restart;
        snprintf(statusText, sizeof(statusText), "CLIENT - connected to host");
    }
    lastRecv = clockTime;

    g->state = state;
    g->round = round;
    g->zombiesToSpawn = toSpawn;
    g->roundBreak = roundBreak;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        p->active = pl[i].active;
        p->alive = pl[i].alive;
        p->health = pl[i].health;
        p->points = pl[i].points;
        p->kills = pl[i].kills;
        if (i != g->localPlayer) { p->x = pl[i].x; p->y = pl[i].y; p->a = pl[i].a; }
    }

    int wasActive[MAX_ZOMBIES];
    for (int i = 0; i < MAX_ZOMBIES; i++) { wasActive[i] = g->zombies[i].active; g->zombies[i].active = 0; }
    for (int n = 0; n < count; n++) {
        int slot = getU8(b);
        float x = getF32(b), y = getF32(b);
        int sprite = getU8(b), flash = getU8(b);
        if (b->bad || slot >= MAX_ZOMBIES) return;
        Zombie *z = &g->zombies[slot];
        z->active = 1;
        z->tx = x; z->ty = y;
        if (!wasActive[slot]) { z->x = x; z->y = y; }
        z->sprite = sprite % NUM_SPRITES;
        z->hurtFlash = flash ? 0.1f : 0;
    }
}

// ---------------------------------------------------------------------------
void net_update(Game *g, float dt)
{
    if (mode == NET_SOLO) return;
    clockTime += dt;

    // Read everything waiting on the socket
    for (;;) {
        Buf b = {0};
        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        int n = (int)recvfrom(sock, b.data, sizeof(b.data), 0, (struct sockaddr *)&from, &fromLen);
        if (n <= 0) break;
        if (n < 4 || getU16(&b) != MAGIC || getU8(&b) != VERSION) continue;
        int type = getU8(&b); // (b starts zeroed, so a short packet just reads zeros)
        if (mode == NET_HOST && type == PKT_CLIENT) hostHandleClient(g, &b, &from);
        if (mode == NET_CLIENT && type == PKT_SNAPSHOT) clientHandleSnapshot(g, &b);
    }

    if (mode == NET_HOST) {
        if (havePeer && !net_connected()) {
            havePeer = 0;
            g->players[1].active = 0;
            snprintf(statusText, sizeof(statusText), "HOST - friend disconnected, waiting...");
            printf("Friend disconnected\n");
        }
        sendTimer -= dt;
        if (havePeer && sendTimer <= 0) { hostSendSnapshot(g); sendTimer = SNAPSHOT_RATE; }
    } else {
        if (lastRecv > 0 && !net_connected())
            snprintf(statusText, sizeof(statusText), "CLIENT - lost connection to host...");
        sendTimer -= dt;
        if (sendTimer <= 0) { clientSendState(g); sendTimer = CLIENT_RATE; }

        // Glide zombies toward their latest network position
        float k = dt * 12.0f > 1.0f ? 1.0f : dt * 12.0f;
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            Zombie *z = &g->zombies[i];
            if (!z->active) continue;
            z->x += (z->tx - z->x) * k;
            z->y += (z->ty - z->y) * k;
        }
    }
}
