#ifndef GLINC_H
#define GLINC_H

// GLUT lives in a different place on macOS and Linux
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#endif
