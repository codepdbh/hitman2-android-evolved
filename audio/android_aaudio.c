/*
 * android_aaudio.c - audio output over AAudio. Opens one low-latency 44.1 kHz stereo 16-bit stream and fills it
 * from the DirectSound mixer (h2_ds_mix). If the device rate differs the system resamples.
 */
#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#include <string.h>

#include "h2_audio.h"
#include "h2_log.h"

static AAudioStream *g_stream;

static aaudio_data_callback_result_t on_audio(AAudioStream *s, void *user, void *data, int32_t frames)
{
    (void)s; (void)user;
    h2_ds_mix((int16_t *)data, (unsigned)frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static int aa_start(void)
{
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) { H2_ERROR(H2_LOG_AUDIO, "AAudio: cannot create a stream builder"); return -1; }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setSampleRate(b, H2_AUDIO_RATE);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setDataCallback(b, on_audio, 0);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &g_stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { H2_ERROR(H2_LOG_AUDIO, "AAudio: cannot open the stream: %s", AAudio_convertResultToText(r)); g_stream = 0; return -1; }
    r = AAudioStream_requestStart(g_stream);
    if (r != AAUDIO_OK) { H2_ERROR(H2_LOG_AUDIO, "AAudio: cannot start the stream: %s", AAudio_convertResultToText(r)); AAudioStream_close(g_stream); g_stream = 0; return -1; }
    H2_INFO(H2_LOG_AUDIO, "AAudio output started: %d Hz, %d ch, buffer %d frames", AAudioStream_getSampleRate(g_stream),
            AAudioStream_getChannelCount(g_stream), AAudioStream_getBufferSizeInFrames(g_stream));
    return 0;
}

static void aa_stop(void)
{
    if (!g_stream) return;
    AAudioStream_requestStop(g_stream);
    AAudioStream_close(g_stream);
    g_stream = 0;
}

static const H2AudioBackend k_aaudio = { "AAudio", aa_start, aa_stop };
const H2AudioBackend *h2_audio_aaudio_backend(void) { return &k_aaudio; }
#endif
