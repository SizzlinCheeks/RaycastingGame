#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);      // never fatal: the game runs silently if audio fails
void audio_shoot(void);
void audio_hurt(void);
void audio_hit(void);
void audio_shutdown(void);

#endif
