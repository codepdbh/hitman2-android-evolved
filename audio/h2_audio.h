/*
 * h2_audio.h - the audio output backend: the one thing that talks to a sound device.
 *
 * platform/dsound.c is the mixer: it owns the DirectSound buffers and, when a backend is installed, produces
 * the output stream by calling back into h2_ds_mix(). A backend only opens a device and asks for samples.
 *   audio/android_aaudio.c   AAudio (Android, API 26+)
 *   none                     headless hosts: dsound.c keeps a real-time clock instead (nothing is audible)
 */
#ifndef H2_AUDIO_H
#define H2_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define H2_AUDIO_RATE 44100          /* output: 44.1 kHz, 16-bit, stereo interleaved */

/* The mixer: fill `frames` stereo frames (frames*2 int16 samples). Called from the backend's audio thread. */
void h2_ds_mix(int16_t *out, unsigned frames);

typedef struct H2AudioBackend {
    const char *name;
    int  (*start)(void);             /* open the output and begin calling h2_ds_mix(); 0 on success */
    void (*stop)(void);
} H2AudioBackend;

void h2_audio_set_backend(const H2AudioBackend *b);
void h2_audio_stop(void);                                 /* close the output (the mixer stops being called) */       /* before the guest creates its DirectSound device */
#if defined(__ANDROID__)
const H2AudioBackend *h2_audio_aaudio_backend(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
