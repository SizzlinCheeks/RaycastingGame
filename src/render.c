#include <stdio.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include <SDL_image.h>
#include "glinc.h"
#include "render.h"
#include "map.h"
#include "net.h"
#include "utils.h"

// 3D view area (right half, top) and HUD (right half, bottom)
#define VIEW_X   512
#define VIEW_W   512
#define VIEW_H   352
#define HUD_Y    VIEW_H
#define COL_W    2
#define NUM_COLS (VIEW_W / COL_W)
#define FOV      60.0f
#define HORIZON  (VIEW_H / 2.0f)

#define MINI_SCALE (512.0f / (mapX * mapS)) // fit the whole map into 512x512

#define SPRITE_SIZE 28.0f  // how tall a sprite is in world units (a wall is 32)

static GLuint spriteTex[NUM_SPRITES];
static GLuint wallTex;
static float zBuffer[NUM_COLS];   // wall distance per screen column
static float rayHitX[NUM_COLS], rayHitY[NUM_COLS];
static float planeLen;            // tan(FOV/2)
static float proj;                // pixels per world unit at distance 1

// ---------------------------------------------------------------------------
// Textures

static GLuint uploadRGBA(int w, int h, const void *pixels)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // GL_NEAREST keeps the chunky 8-bit pixels sharp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static GLuint loadSprite(const char *path)
{
    SDL_Surface *loaded = IMG_Load(path);
    if (!loaded) {
        // Don't crash: use a magenta "missing texture" box instead
        printf("Could not load %s: %s\n", path, IMG_GetError());
        unsigned char px[8 * 8 * 4];
        for (int i = 0; i < 64; i++) {
            int on = ((i % 8) / 4 + (i / 8) / 4) % 2;
            px[i * 4 + 0] = on ? 255 : 0; px[i * 4 + 1] = 0;
            px[i * 4 + 2] = on ? 255 : 0; px[i * 4 + 3] = 255;
        }
        return uploadRGBA(8, 8, px);
    }
    // PNGs come in many formats (palette, RGB, RGBA...). Convert to plain RGBA
    // bytes so OpenGL always gets what we tell it we are giving it.
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!rgba) { printf("Convert failed for %s: %s\n", path, SDL_GetError()); return 0; }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
    GLuint t = uploadRGBA(rgba->w, rgba->h, rgba->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    SDL_FreeSurface(rgba);
    return t;
}

static GLuint makeBrickTexture(void)
{
    // 16x16 pixel brick wall, 8 px wide x 4 px tall bricks
    unsigned char px[16 * 16 * 4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int row = y / 4;
            int bx = (x + (row % 2) * 4) % 8;
            int mortar = (y % 4 == 3) || (bx == 7);
            int brickId = (x + (row % 2) * 4) / 8 + row * 3;
            unsigned char *p = &px[(y * 16 + x) * 4];
            if (mortar) { p[0] = 70; p[1] = 64; p[2] = 60; }
            else {
                int shade = (brickId * 37) % 3;  // a bit of variety per brick
                static const unsigned char pal[3][3] = { {150, 60, 40}, {130, 50, 36}, {168, 74, 48} };
                p[0] = pal[shade][0]; p[1] = pal[shade][1]; p[2] = pal[shade][2];
                if (y % 4 == 0) { p[0] += 30; p[1] += 20; p[2] += 15; } // top highlight
            }
            p[3] = 255;
        }
    }
    GLuint t = uploadRGBA(16, 16, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

void render_init(void)
{
    planeLen = tanf(degToRad(FOV / 2));
    proj = (VIEW_W / 2.0f) / planeLen;

    glClearColor(0, 0, 0, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glAlphaFunc(GL_GREATER, 0.5f); // fully skip transparent sprite pixels

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
        printf("IMG_Init failed: %s\n", IMG_GetError());

    char path[64];
    for (int i = 0; i < NUM_SPRITES; i++) {
        snprintf(path, sizeof(path), "assets/sprites/enemy%d.png", i + 1);
        spriteTex[i] = loadSprite(path);
    }
    wallTex = makeBrickTexture();
}

// ---------------------------------------------------------------------------
// Small drawing helpers

static void rect(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

static int textWidth(void *font, const char *s)
{
    int w = 0;
    for (; *s; s++) w += glutBitmapWidth(font, *s);
    return w;
}

static void text(float x, float y, void *font, const char *s)
{
    glRasterPos2f(x, y);
    for (; *s; s++) glutBitmapCharacter(font, *s);
}

static void textCentered(float cx, float y, void *font, const char *s)
{
    text(cx - textWidth(font, s) / 2.0f, y, font, s);
}

// ---------------------------------------------------------------------------
// 3D view

static void drawWalls(const Player *me)
{
    float dirX = cosf(degToRad(me->a)), dirY = -sinf(degToRad(me->a));
    float rightX = -dirY, rightY = dirX;

    // Ceiling and floor with a simple gradient
    glBegin(GL_QUADS);
    glColor3f(0.05f, 0.04f, 0.08f); glVertex2f(VIEW_X, 0); glVertex2f(VIEW_X + VIEW_W, 0);
    glColor3f(0.0f, 0.0f, 0.0f);    glVertex2f(VIEW_X + VIEW_W, HORIZON); glVertex2f(VIEW_X, HORIZON);
    glColor3f(0.0f, 0.0f, 0.0f);    glVertex2f(VIEW_X, HORIZON); glVertex2f(VIEW_X + VIEW_W, HORIZON);
    glColor3f(0.25f, 0.22f, 0.2f);  glVertex2f(VIEW_X + VIEW_W, VIEW_H); glVertex2f(VIEW_X, VIEW_H);
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, wallTex);
    glBegin(GL_QUADS);
    for (int c = 0; c < NUM_COLS; c++) {
        // camX goes from -1 (left edge) to +1 (right edge)
        float camX = 2.0f * (c + 0.5f) / NUM_COLS - 1.0f;
        float rdx = dirX + rightX * planeLen * camX;
        float rdy = dirY + rightY * planeLen * camX;
        int side;
        float u;
        // Because (rdx, rdy) has length 1 along the view direction,
        // this distance is already "perpendicular": no fisheye fix needed.
        float dist = mapRaycast(me->x, me->y, rdx, rdy, &side, &u);
        if (dist < 0.5f) dist = 0.5f;
        zBuffer[c] = dist;
        rayHitX[c] = me->x + rdx * dist;
        rayHitY[c] = me->y + rdy * dist;
        if ((side == 0 && rdx < 0) || (side == 1 && rdy > 0)) u = 1.0f - u;

        float lineH = proj * mapS / dist;
        float top = HORIZON - lineH / 2, bottom = HORIZON + lineH / 2;
        float y0 = fmaxf(top, 0), y1 = fminf(bottom, VIEW_H);
        float v0 = (y0 - top) / lineH * 2.0f, v1 = (y1 - top) / lineH * 2.0f; // texture repeats twice vertically
        float fog = clampf(1.0f - dist / (mapS * 14.0f), 0.2f, 1.0f);
        float shade = (side ? 0.7f : 1.0f) * fog;
        glColor3f(shade, shade, shade);

        float x0 = VIEW_X + c * COL_W, x1 = x0 + COL_W;
        glTexCoord2f(u * 2, v0); glVertex2f(x0, y0);
        glTexCoord2f(u * 2, v0); glVertex2f(x1, y0);
        glTexCoord2f(u * 2, v1); glVertex2f(x1, y1);
        glTexCoord2f(u * 2, v1); glVertex2f(x0, y1);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

typedef struct { float x, y, depth; int sprite; int flash; } SpriteDraw;

static void drawSprites(const Game *g, const Player *me)
{
    SpriteDraw list[MAX_ZOMBIES + MAX_PLAYERS];
    int n = 0;
    float dirX = cosf(degToRad(me->a)), dirY = -sinf(degToRad(me->a));

    for (int i = 0; i < MAX_ZOMBIES; i++) {
        const Zombie *z = &g->zombies[i];
        if (!z->active) continue;
        list[n++] = (SpriteDraw){ z->x, z->y, 0, z->sprite, z->hurtFlash > 0 };
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Player *p = &g->players[i];
        if (i == g->localPlayer || !p->active || !p->alive) continue;
        list[n++] = (SpriteDraw){ p->x, p->y, 0, SPR_PLAYER, 0 };
    }
    for (int i = 0; i < n; i++)
        list[i].depth = (list[i].x - me->x) * dirX + (list[i].y - me->y) * dirY;

    // Painter's algorithm: draw far sprites first
    for (int i = 1; i < n; i++) {
        SpriteDraw t = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].depth < t.depth) { list[j + 1] = list[j]; j--; }
        list[j + 1] = t;
    }

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    float rightX = -dirY, rightY = dirX;
    for (int i = 0; i < n; i++) {
        SpriteDraw *s = &list[i];
        if (s->depth < 2.0f) continue; // behind us or inside our face
        float side = (s->x - me->x) * rightX + (s->y - me->y) * rightY;
        float screenX = VIEW_W / 2.0f + side * proj / s->depth;
        float size = proj * SPRITE_SIZE / s->depth;
        float bottom = HORIZON + proj * (mapS / 2.0f) / s->depth; // feet on the floor
        float top = bottom - size;
        float left = screenX - size / 2;
        float y0 = fmaxf(top, 0), y1 = fminf(bottom, VIEW_H);
        if (y1 <= y0) continue;
        float v0 = (y0 - top) / size, v1 = (y1 - top) / size;

        float fog = clampf(1.0f - s->depth / (mapS * 14.0f), 0.25f, 1.0f);
        if (s->flash) glColor3f(1, 0.3f, 0.3f);
        else glColor3f(fog, fog, fog);
        glBindTexture(GL_TEXTURE_2D, spriteTex[s->sprite]);

        int c0 = (int)floorf(left / COL_W), c1 = (int)ceilf((left + size) / COL_W);
        if (c0 < 0) c0 = 0;
        if (c1 > NUM_COLS) c1 = NUM_COLS;
        // Draw the sprite in strips, skipping columns hidden behind a wall
        glBegin(GL_QUADS);
        for (int c = c0; c < c1; c++) {
            if (s->depth >= zBuffer[c]) continue;
            float sx0 = fmaxf(c * COL_W, left), sx1 = fminf((c + 1) * COL_W, left + size);
            float u0 = (sx0 - left) / size, u1 = (sx1 - left) / size;
            glTexCoord2f(u0, v0); glVertex2f(VIEW_X + sx0, y0);
            glTexCoord2f(u1, v0); glVertex2f(VIEW_X + sx1, y0);
            glTexCoord2f(u1, v1); glVertex2f(VIEW_X + sx1, y1);
            glTexCoord2f(u0, v1); glVertex2f(VIEW_X + sx0, y1);
        }
        glEnd();
    }
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
}

static void drawGun(const Game *g)
{
    // 8-bit pistol, seen from behind. One character = one big pixel.
    static const char *art[] = {
        "......KK......",
        ".....KGGK.....",
        ".....KGGK.....",
        "....KGLGGK....",
        "....KGLGGK....",
        "....KGLGGK....",
        "....KGDDGK....",
        "...SKGGGGKS...",
        "..SSKKKKKKSS..",
        ".SSSSSSSSSSSS.",
        ".SSSHSSSSHSSS.",
        "SSSSSSSSSSSSSS",
    };
    const int rows = sizeof(art) / sizeof(art[0]), cols = 14;
    const float px = 9;
    float recoil = g->muzzleFlash > 0 ? 14 : 0;
    float reloadDip = g->reloadTimer > 0 ? 60 * sinf(PI * g->reloadTimer / RELOAD_TIME) : 0;
    float ox = VIEW_X + VIEW_W / 2.0f - cols * px / 2;
    float oy = VIEW_H - rows * px + recoil + reloadDip;

    if (g->muzzleFlash > 0) {
        float cx = VIEW_X + VIEW_W / 2.0f, cy = oy - 18;
        glColor3f(1, 0.9f, 0.2f); rect(cx - 18, cy - 9, cx + 18, cy + 9); rect(cx - 9, cy - 22, cx + 9, cy + 22);
        glColor3f(1, 1, 0.8f);    rect(cx - 8, cy - 8, cx + 8, cy + 8);
    }

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            switch (art[r][c]) {
                case 'K': glColor3f(0.05f, 0.05f, 0.05f); break;
                case 'G': glColor3f(0.45f, 0.45f, 0.5f); break;
                case 'L': glColor3f(0.7f, 0.7f, 0.75f); break;
                case 'D': glColor3f(0.25f, 0.25f, 0.28f); break;
                case 'S': glColor3f(0.85f, 0.6f, 0.45f); break;
                case 'H': glColor3f(0.65f, 0.42f, 0.3f); break;
                default: continue;
            }
            float x = ox + c * px, y = oy + r * px;
            if (y >= VIEW_H) continue;
            rect(x, y, x + px, fminf(y + px, VIEW_H));
        }
    }
}

static void drawView(const Game *g)
{
    const Player *me = &g->players[g->localPlayer];
    drawWalls(me);
    drawSprites(g, me);
    drawGun(g);

    float cx = VIEW_X + VIEW_W / 2.0f, cy = HORIZON;
    // Crosshair
    glColor3f(1, 1, 1);
    rect(cx - 1, cy - 8, cx + 1, cy - 3); rect(cx - 1, cy + 3, cx + 1, cy + 8);
    rect(cx - 8, cy - 1, cx - 3, cy + 1); rect(cx + 3, cy - 1, cx + 8, cy + 1);
    // Hit marker
    if (g->hitMarker > 0) {
        glLineWidth(2);
        glBegin(GL_LINES);
        glVertex2f(cx - 10, cy - 10); glVertex2f(cx - 5, cy - 5);
        glVertex2f(cx + 10, cy - 10); glVertex2f(cx + 5, cy - 5);
        glVertex2f(cx - 10, cy + 10); glVertex2f(cx - 5, cy + 5);
        glVertex2f(cx + 10, cy + 10); glVertex2f(cx + 5, cy + 5);
        glEnd();
    }

    // Red screen when hurt or low on health
    float red = g->hurtFlash * 0.6f;
    if (me->alive && me->health < 40) red = fmaxf(red, 0.15f + 0.1f * sinf(g->time * 6));
    if (red > 0) { glColor4f(0.8f, 0, 0, red); rect(VIEW_X, 0, VIEW_X + VIEW_W, VIEW_H); }

    // Messages in the middle of the screen
    char buf[96];
    if (g->state == STATE_GAMEOVER) {
        glColor4f(0, 0, 0, 0.7f); rect(VIEW_X, 0, VIEW_X + VIEW_W, VIEW_H);
        glColor3f(0.9f, 0.1f, 0.1f);
        textCentered(cx, 140, GLUT_BITMAP_TIMES_ROMAN_24, "GAME OVER");
        glColor3f(1, 1, 1);
        snprintf(buf, sizeof(buf), "You made it to round %d", g->round);
        textCentered(cx, 180, GLUT_BITMAP_HELVETICA_18, buf);
        textCentered(cx, 215, GLUT_BITMAP_HELVETICA_18,
                     net_mode() == NET_CLIENT ? "Waiting for the host to restart..." : "Press ENTER to play again");
    } else if (!me->alive) {
        glColor4f(0, 0, 0, 0.5f); rect(VIEW_X, 0, VIEW_X + VIEW_W, VIEW_H);
        glColor3f(0.9f, 0.2f, 0.2f);
        textCentered(cx, 150, GLUT_BITMAP_TIMES_ROMAN_24, "YOU'RE DOWN");
        glColor3f(1, 1, 1);
        textCentered(cx, 185, GLUT_BITMAP_HELVETICA_18, "You'll be back next round if your friend survives");
    } else if (g->roundBreak > 0) {
        glColor3f(0.9f, 0.1f, 0.1f);
        if (g->round == 0) snprintf(buf, sizeof(buf), "GET READY...");
        else snprintf(buf, sizeof(buf), "ROUND %d SURVIVED", g->round);
        textCentered(cx, 80, GLUT_BITMAP_TIMES_ROMAN_24, buf);
    }
}

// ---------------------------------------------------------------------------
// Minimap (left half)

static void drawMinimap(const Game *g)
{
    const float s = mapS * MINI_SCALE;
    for (int y = 0; y < mapY; y++) {
        for (int x = 0; x < mapX; x++) {
            if (map[y][x] > 0) glColor3f(0.55f, 0.25f, 0.18f);
            else glColor3f(0.08f, 0.08f, 0.08f);
            rect(x * s + 1, y * s + 1, (x + 1) * s - 1, (y + 1) * s - 1);
        }
    }

    const Player *me = &g->players[g->localPlayer];
    // A few of the rays we cast
    glColor4f(0.2f, 0.8f, 0.2f, 0.35f);
    glLineWidth(1);
    glBegin(GL_LINES);
    for (int c = 0; c < NUM_COLS; c += 8) {
        glVertex2f(me->x * MINI_SCALE, me->y * MINI_SCALE);
        glVertex2f(rayHitX[c] * MINI_SCALE, rayHitY[c] * MINI_SCALE);
    }
    glEnd();

    glPointSize(8);
    glBegin(GL_POINTS);
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        const Zombie *z = &g->zombies[i];
        if (!z->active) continue;
        glColor3f(z->hurtFlash > 0 ? 1.0f : 0.9f, z->hurtFlash > 0 ? 1.0f : 0.1f, 0.1f);
        glVertex2f(z->x * MINI_SCALE, z->y * MINI_SCALE);
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Player *p = &g->players[i];
        if (!p->active) continue;
        if (i == g->localPlayer) glColor3f(1, 1, 0);
        else glColor3f(0.2f, 0.6f, 1.0f);
        if (!p->alive) glColor3f(0.4f, 0.4f, 0.4f);
        glVertex2f(p->x * MINI_SCALE, p->y * MINI_SCALE);
    }
    glEnd();

    glColor3f(1, 1, 0);
    glLineWidth(3);
    glBegin(GL_LINES);
    glVertex2f(me->x * MINI_SCALE, me->y * MINI_SCALE);
    glVertex2f((me->x + cosf(degToRad(me->a)) * 20) * MINI_SCALE,
               (me->y - sinf(degToRad(me->a)) * 20) * MINI_SCALE);
    glEnd();
}

// ---------------------------------------------------------------------------
// HUD (bottom right)

static void drawHud(const Game *g)
{
    const Player *me = &g->players[g->localPlayer];
    char buf[128];

    glColor3f(0.07f, 0.05f, 0.05f);
    rect(VIEW_X, HUD_Y, VIEW_X + VIEW_W, SCREEN_H);
    glColor3f(0.4f, 0.1f, 0.1f);
    rect(VIEW_X, HUD_Y, VIEW_X + VIEW_W, HUD_Y + 3);

    float x = VIEW_X + 16;
    glColor3f(0.85f, 0.05f, 0.05f);
    snprintf(buf, sizeof(buf), "ROUND %d", g->round);
    text(x, HUD_Y + 32, GLUT_BITMAP_TIMES_ROMAN_24, buf);

    // Health bar
    glColor3f(0.25f, 0.05f, 0.05f); rect(x, HUD_Y + 44, x + 200, HUD_Y + 60);
    glColor3f(0.85f, 0.15f, 0.15f); rect(x, HUD_Y + 44, x + 200 * me->health / (float)PLAYER_MAX_HP, HUD_Y + 60);
    glColor3f(1, 1, 1);
    snprintf(buf, sizeof(buf), "HP %d", me->health);
    text(x + 6, HUD_Y + 57, GLUT_BITMAP_HELVETICA_12, buf);

    glColor3f(1, 0.85f, 0.2f);
    snprintf(buf, sizeof(buf), "POINTS %d", me->points);
    text(x, HUD_Y + 85, GLUT_BITMAP_HELVETICA_18, buf);

    if (g->reloadTimer > 0) { glColor3f(1, 1, 1); snprintf(buf, sizeof(buf), "RELOADING..."); }
    else if (g->mag == 0 && g->reserve == 0) { glColor3f(1, 0.3f, 0.3f); snprintf(buf, sizeof(buf), "NO AMMO"); }
    else if (g->mag == 0) { glColor3f(1, 0.3f, 0.3f); snprintf(buf, sizeof(buf), "PRESS R TO RELOAD"); }
    else { glColor3f(1, 1, 1); snprintf(buf, sizeof(buf), "AMMO %d / %d", g->mag, g->reserve); }
    text(x, HUD_Y + 110, GLUT_BITMAP_HELVETICA_18, buf);

    float x2 = VIEW_X + 270;
    glColor3f(0.8f, 0.8f, 0.8f);
    snprintf(buf, sizeof(buf), "ZOMBIES LEFT  %d", game_zombies_remaining(g));
    text(x2, HUD_Y + 30, GLUT_BITMAP_HELVETICA_12, buf);
    snprintf(buf, sizeof(buf), "KILLS  %d", me->kills);
    text(x2, HUD_Y + 48, GLUT_BITMAP_HELVETICA_12, buf);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Player *p = &g->players[i];
        if (i == g->localPlayer || !p->active) continue;
        glColor3f(0.3f, 0.65f, 1.0f);
        if (p->alive) snprintf(buf, sizeof(buf), "FRIEND  %d HP  %d PTS", p->health, p->points);
        else snprintf(buf, sizeof(buf), "FRIEND IS DOWN!");
        text(x2, HUD_Y + 66, GLUT_BITMAP_HELVETICA_12, buf);
    }

    glColor3f(0.5f, 0.5f, 0.5f);
    text(x2, HUD_Y + 92, GLUT_BITMAP_HELVETICA_10, net_status());
    text(x, SCREEN_H - 12, GLUT_BITMAP_HELVETICA_10,
         "WASD/arrows move  Q/E strafe  M sprint  SPACE/click shoot  R reload  X quit");
}

void render_frame(const Game *g)
{
    glClear(GL_COLOR_BUFFER_BIT);
    drawView(g);      // must run first: it fills zBuffer and the ray hits
    drawMinimap(g);
    drawHud(g);
}
