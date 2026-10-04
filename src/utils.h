#ifndef UTILS_H
#define UTILS_H

#define PI 3.14159265358979f

float degToRad(float a);
float FixAng(float a);          // wrap an angle in degrees to [0, 360)
float clampf(float v, float lo, float hi);
float randf(float lo, float hi);

#endif
