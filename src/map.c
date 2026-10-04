#include <math.h>
#include "map.h"

int map[mapY][mapX] = {
     {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
     {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,0,1},
     {1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,1},
     {1,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,1},
     {1,0,0,1,0,0,0,1,0,0,0,0,0,1,1,0,1,1,0,1},
     {1,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
     {1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
     {1,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
     {1,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
     {1,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
     {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
     {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

int mapCellIsWall(int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= mapX || cy >= mapY) return 1;
    return map[cy][cx] > 0;
}

int mapIsWall(float x, float y)
{
    return mapCellIsWall((int)floorf(x / mapS), (int)floorf(y / mapS));
}

int mapCollides(float x, float y, float r)
{
    return mapIsWall(x - r, y - r) || mapIsWall(x + r, y - r) ||
           mapIsWall(x - r, y + r) || mapIsWall(x + r, y + r);
}

void mapMove(float *x, float *y, float dx, float dy, float r)
{
    // Try each axis separately so you slide along walls instead of sticking.
    if (!mapCollides(*x + dx, *y, r)) *x += dx;
    if (!mapCollides(*x, *y + dy, r)) *y += dy;
}

float mapRaycast(float ox, float oy, float dx, float dy, int *side, float *wallU)
{
    // Work in cell units, then convert back to world units at the end.
    float px = ox / mapS, py = oy / mapS;
    int mx = (int)floorf(px), my = (int)floorf(py);
    float deltaX = (dx == 0) ? 1e30f : fabsf(1.0f / dx);
    float deltaY = (dy == 0) ? 1e30f : fabsf(1.0f / dy);
    int stepX, stepY, s = 0;
    float sideX, sideY;

    if (dx < 0) { stepX = -1; sideX = (px - mx) * deltaX; }
    else        { stepX =  1; sideX = (mx + 1.0f - px) * deltaX; }
    if (dy < 0) { stepY = -1; sideY = (py - my) * deltaY; }
    else        { stepY =  1; sideY = (my + 1.0f - py) * deltaY; }

    for (int i = 0; i < mapX + mapY + 2; i++) {
        if (sideX < sideY) { sideX += deltaX; mx += stepX; s = 0; }
        else               { sideY += deltaY; my += stepY; s = 1; }
        if (mapCellIsWall(mx, my)) break;
    }

    float t = (s == 0) ? sideX - deltaX : sideY - deltaY;
    if (side) *side = s;
    if (wallU) {
        float w = (s == 0) ? py + t * dy : px + t * dx;
        *wallU = w - floorf(w);
    }
    return t * mapS;
}
