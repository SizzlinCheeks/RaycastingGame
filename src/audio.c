#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include "audio.h"

#define RATE 44100

static int ready = 0;
static Mix_Music *music = NULL;
static Mix_Chunk *sfxShoot = NULL, *sfxHurt = NULL, *sfxHit = NULL;

// Build a little 8-bit style sound effect in memory (16-bit stereo).
// kind 0 = noise burst (gunshot), 1 = square wave sweep down, 2 = short blip
static Mix_Chunk *makeSfx(int kind, float seconds, float freq)
{
    int frames = (int)(RATE * seconds);
    Sint16 *buf = malloc(frames * 2 * sizeof(Sint16));
    if (!buf) return NULL;
    float phase = 0;
    Sint16 noise = 0;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / frames;
        float env = (1.0f - t) * (1.0f - t);
        float s;
        if (kind == 0) {
            if (i % 6 == 0) noise = (Sint16)((rand() % 2) ? 1 : -1); // crunchy low-rate noise
            s = noise;
        } else {
            float f = (kind == 1) ? freq * (1.0f - 0.6f * t) : freq;
            phase += f / RATE;
            s = (phase - floorf(phase)) < 0.5f ? 1.0f : -1.0f;
        }
        Sint16 v = (Sint16)(s * env * 9000);
        buf[i * 2] = buf[i * 2 + 1] = v;
    }
    // QuickLoad_RAW does not copy, so buf is intentionally kept alive.
    return Mix_QuickLoad_RAW((Uint8 *)buf, frames * 2 * sizeof(Sint16));
}

void audio_init(void)
{
    // Only init AUDIO here. GLUT owns the window, so SDL video must stay off.
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("Audio disabled (SDL_Init): %s\n", SDL_GetError());
        return;
    }
    if (Mix_OpenAudioDevice(RATE, AUDIO_S16SYS, 2, 1024, NULL, 0) < 0) {
        printf("Audio disabled (Mix_OpenAudio): %s\n", Mix_GetError());
        return;
    }
    ready = 1;
    Mix_Init(MIX_INIT_MP3);

    sfxShoot = makeSfx(0, 0.18f, 0);
    sfxHurt  = makeSfx(1, 0.30f, 220);
    sfxHit   = makeSfx(2, 0.05f, 880);

    music = Mix_LoadMUS("assets/music/background.mp3");
    if (music) {
        Mix_VolumeMusic(MIX_MAX_VOLUME / 2);
        Mix_PlayMusic(music, -1);
    } else {
        printf("No background music (%s) - that's fine.\n", Mix_GetError());
    }
}

static void play(Mix_Chunk *c) { if (ready && c) Mix_PlayChannel(-1, c, 0); }
void audio_shoot(void) { play(sfxShoot); }
void audio_hurt(void)  { play(sfxHurt); }
void audio_hit(void)   { play(sfxHit); }

void audio_shutdown(void)
{
    if (!ready) return;
    Mix_HaltChannel(-1);
    if (music) Mix_FreeMusic(music);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    ready = 0;
}
