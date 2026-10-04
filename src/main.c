// 8-bit raycasting zombies. Survive the rounds, alone or with a friend.
//
//   ./zombies                     play solo
//   ./zombies host [port]         host a co-op game
//   ./zombies join <ip> [port]    join a friend's game

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include "glinc.h"
#include "game.h"
#include "map.h"
#include "render.h"
#include "audio.h"
#include "net.h"
#include "utils.h"

typedef struct {
    int w, a, s, d, q, e, m;
    int up, down, left, right;
} Keys;

static Game game;
static Keys keys;
static int lastTime;
static int prevHealth, prevPoints, prevRound;

static void quit(void)
{
    net_shutdown();
    audio_shutdown();
    exit(0);
}

static void startReload(void)
{
    if (game.reloadTimer <= 0 && game.mag < MAG_SIZE && game.reserve > 0)
        game.reloadTimer = RELOAD_TIME;
}

static void fire(void)
{
    Player *me = &game.players[game.localPlayer];
    if (!me->alive || game.state != STATE_PLAYING) return;
    if (game.reloadTimer > 0 || game.fireCooldown > 0) return;
    if (game.mag == 0) { startReload(); return; }

    game.mag--;
    game.fireCooldown = FIRE_DELAY;
    game.muzzleFlash = 0.08f;
    game.shotCounter++;   // the client sends this to the host
    audio_shoot();
    if (net_mode() != NET_CLIENT) game_fire(&game, game.localPlayer);
}

static void updateLocalPlayer(float dt)
{
    Player *me = &game.players[game.localPlayer];

    if (game.muzzleFlash > 0) game.muzzleFlash -= dt;
    if (game.hurtFlash > 0) game.hurtFlash -= dt;
    if (game.hitMarker > 0) game.hitMarker -= dt;
    if (game.fireCooldown > 0) game.fireCooldown -= dt;
    if (game.reloadTimer > 0) {
        game.reloadTimer -= dt;
        if (game.reloadTimer <= 0) {
            int take = MAG_SIZE - game.mag;
            if (take > game.reserve) take = game.reserve;
            game.mag += take;
            game.reserve -= take;
            game.reloadTimer = 0;
        }
    }

    if (!me->alive || game.state != STATE_PLAYING) return;

    // Turning
    if (keys.a || keys.left)  me->a = FixAng(me->a + TURN_SPEED * dt);
    if (keys.d || keys.right) me->a = FixAng(me->a - TURN_SPEED * dt);

    // Moving (forward/back + strafe), sliding along walls
    float dirX = cosf(degToRad(me->a)), dirY = -sinf(degToRad(me->a));
    float rightX = -dirY, rightY = dirX;
    float fwd = (float)((keys.w || keys.up) - (keys.s || keys.down));
    float strafe = (float)(keys.e - keys.q);
    float mx = dirX * fwd + rightX * strafe;
    float my = dirY * fwd + rightY * strafe;
    float len = hypotf(mx, my);
    if (len > 0) {
        float speed = PLAYER_SPEED * (keys.m ? SPRINT_MULT : 1.0f) * dt / len;
        mapMove(&me->x, &me->y, mx * speed, my * speed, PLAYER_RADIUS);
    }
}

// Game logic runs from the timer, not from display(): GLUT skips display()
// while the window is hidden, and the host must keep simulating for its friend.
static void update(void)
{
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - lastTime) / 1000.0f;
    lastTime = now;
    if (dt > 0.05f) dt = 0.05f; // a hiccup must not teleport anyone through walls

    updateLocalPlayer(dt);
    if (net_mode() == NET_CLIENT) game.time += dt;  // the host simulates the world
    else game_update(&game, dt);
    net_update(&game, dt);

    // React to changes, no matter whether they happened here or on the host
    Player *me = &game.players[game.localPlayer];
    if (me->health < prevHealth) { game.hurtFlash = 0.4f; audio_hurt(); }
    if (me->points > prevPoints) { game.hitMarker = 0.15f; audio_hit(); }
    if (game.round != prevRound && game.round > 0) game.reserve = MAX_RESERVE; // max ammo each round
    prevHealth = me->health;
    prevPoints = me->points;
    prevRound = game.round;
}

static void display(void)
{
    render_frame(&game);
    glutSwapBuffers();
}

static void tick(int value)
{
    (void)value;
    update();
    glutPostRedisplay();
    glutTimerFunc(16, tick, 0); // ~60 fps
}

static void reshape(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, SCREEN_W, SCREEN_H, 0); // y grows downward, like the map
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void setKey(unsigned char key, int down)
{
    switch (tolower(key)) {
        case 'w': keys.w = down; break;
        case 'a': keys.a = down; break;
        case 's': keys.s = down; break;
        case 'd': keys.d = down; break;
        case 'q': keys.q = down; break;
        case 'e': keys.e = down; break;
        case 'm': keys.m = down; break;
    }
}

static void keyDown(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    setKey(key, 1);
    if (key == ' ') fire();
    if (tolower(key) == 'r') startReload();
    if (tolower(key) == 'x' || key == 27) quit();
    if ((key == 13 || key == '\n') && game.state == STATE_GAMEOVER && net_mode() != NET_CLIENT)
        game_restart(&game);
}

static void keyUp(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    setKey(key, 0);
}

static void special(int key, int down)
{
    if (key == GLUT_KEY_UP) keys.up = down;
    if (key == GLUT_KEY_DOWN) keys.down = down;
    if (key == GLUT_KEY_LEFT) keys.left = down;
    if (key == GLUT_KEY_RIGHT) keys.right = down;
}
static void specialDown(int key, int x, int y) { (void)x; (void)y; special(key, 1); }
static void specialUp(int key, int x, int y) { (void)x; (void)y; special(key, 0); }

static void mouse(int button, int state, int x, int y)
{
    (void)x; (void)y;
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) fire();
}

static void usage(const char *prog)
{
    printf("Usage:\n  %s                    play solo\n"
           "  %s host [port]        host a co-op game (default port %d)\n"
           "  %s join <ip> [port]   join a friend's game\n", prog, prog, DEFAULT_PORT, prog);
}

int main(int argc, char *argv[])
{
    glutInit(&argc, argv); // removes GLUT's own options from argv
    setvbuf(stdout, NULL, _IOLBF, 0);
    srand((unsigned)time(NULL));
    game_init(&game);

    if (argc >= 2 && strcmp(argv[1], "host") == 0) {
        if (!net_host(argc >= 3 ? atoi(argv[2]) : DEFAULT_PORT)) return 1;
    } else if (argc >= 3 && strcmp(argv[1], "join") == 0) {
        if (!net_join(argv[2], argc >= 4 ? atoi(argv[3]) : DEFAULT_PORT)) return 1;
        game.localPlayer = 1;              // host is player 0, we are player 1
        game.players[1].active = 1;
        game_spawn_player(&game, 1);
    } else if (argc >= 2) {
        usage(argv[0]);
        return 1;
    }

    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(SCREEN_W, SCREEN_H);
    glutInitWindowPosition(100, 100);
    glutCreateWindow(net_mode() == NET_CLIENT ? "8-Bit Zombies (client)"
                   : net_mode() == NET_HOST ? "8-Bit Zombies (host)" : "8-Bit Zombies");

    render_init();   // needs the GL context, so it comes after glutCreateWindow
    audio_init();

    Player *me = &game.players[game.localPlayer];
    prevHealth = me->health;
    prevPoints = me->points;
    prevRound = game.round;
    lastTime = glutGet(GLUT_ELAPSED_TIME);

    glutIgnoreKeyRepeat(1); // otherwise holding a key sends endless down/up pairs
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutSpecialFunc(specialDown);
    glutSpecialUpFunc(specialUp);
    glutMouseFunc(mouse);
    glutTimerFunc(16, tick, 0);
    glutMainLoop();
    return 0;
}
