#include <math.h>
#include <stdlib.h>
#include "utils.h"

float degToRad(float a) { return a * PI / 180.0f; }

float FixAng(float a)
{
    a = fmodf(a, 360.0f);
    if (a < 0) a += 360.0f;
    return a;
}

float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float randf(float lo, float hi)
{
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}
