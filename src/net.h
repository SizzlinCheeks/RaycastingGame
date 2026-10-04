#ifndef NET_H
#define NET_H

#include "game.h"

#define DEFAULT_PORT 7777

enum { NET_SOLO, NET_HOST, NET_CLIENT };

// Simple 2-player co-op over UDP.
// The host runs the real game (zombies, rounds, damage) and sends snapshots.
// The client moves its own player and tells the host where it is and when it shoots.
int  net_host(int port);
int  net_join(const char *address, int port);
void net_update(Game *g, float dt);   // call once per frame
void net_shutdown(void);

int  net_mode(void);
int  net_connected(void);
const char *net_status(void);

#endif
