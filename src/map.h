#ifndef MAP_H
#define MAP_H

#define mapX 20 // map width in cells
#define mapY 20 // map height in cells
#define mapS 32 // size of one cell in world units

// Declared here, defined exactly once in map.c. Indexed map[y][x].
extern int map[mapY][mapX];

int mapCellIsWall(int cx, int cy);           // out of bounds counts as wall
int mapIsWall(float x, float y);             // world coordinates
int mapCollides(float x, float y, float r);  // does a box of radius r hit a wall?

// Move (x, y) by (dx, dy), sliding along walls. r is the body radius.
void mapMove(float *x, float *y, float dx, float dy, float r);

// DDA ray cast from world point (ox, oy) in direction (dx, dy).
// Returns t such that the hit point is (ox + t*dx, oy + t*dy).
// If (dx, dy) is a unit vector, t is the distance to the wall.
// side: 0 = hit a vertical (x) wall face, 1 = horizontal (y) face.
// wallU: 0..1 position along the wall face, used for texturing.
float mapRaycast(float ox, float oy, float dx, float dy, int *side, float *wallU);

#endif
