#include "startup_audio.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

static HWAVEOUT g_wave_out;
static WAVEHDR g_wave_hdr;
static short *g_pcm_data;
static int g_pcm_samples;
static int g_playing;

static void CALLBACK wave_out_proc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance,
                                   DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)hwo; (void)dwInstance; (void)dwParam1; (void)dwParam2;
    if (uMsg == WOM_DONE) {
        g_playing = 0;
    }
}

void startup_audio_play(const char *exe_dir) {
    if (g_playing) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/assets/rmgbe_startup.mp3", exe_dir);

    FILE *f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0) { fclose(f); return; }

    unsigned char *mp3_buf = malloc(file_size);
    if (!mp3_buf) { fclose(f); return; }
    fread(mp3_buf, 1, file_size, f);
    fclose(f);

    mp3dec_t dec;
    mp3dec_frame_info_t info;
    mp3dec_init(&dec);

    int total_alloc = 0;
    short *all_samples = NULL;
    int pos = 0;

    while (pos < file_size) {
        short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int samples = mp3dec_decode_frame(&dec, mp3_buf + pos, (int)(file_size - pos), pcm, &info);
        pos += info.frame_bytes;
        if (samples <= 0) continue;

        int new_size = total_alloc + samples * info.channels * sizeof(short);
        all_samples = realloc(all_samples, new_size);
        if (!all_samples) break;
        memcpy(all_samples + total_alloc / sizeof(short), pcm, samples * info.channels * sizeof(short));
        total_alloc += samples * info.channels * sizeof(short);
    }
    free(mp3_buf);

    if (!all_samples || total_alloc == 0) { free(all_samples); return; }

    g_pcm_data = all_samples;
    g_pcm_samples = total_alloc / sizeof(short);

    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = info.channels;
    wfx.nSamplesPerSec = info.hz;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    if (waveOutOpen(&g_wave_out, WAVE_MAPPER, &wfx, (DWORD_PTR)wave_out_proc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        free(all_samples);
        g_pcm_data = NULL;
        return;
    }

    memset(&g_wave_hdr, 0, sizeof(g_wave_hdr));
    g_wave_hdr.lpData = (LPSTR)g_pcm_data;
    g_wave_hdr.dwBufferLength = total_alloc;
    g_wave_hdr.dwFlags = 0;

    if (waveOutPrepareHeader(g_wave_out, &g_wave_hdr, sizeof(g_wave_hdr)) != MMSYSERR_NOERROR) {
        waveOutClose(g_wave_out);
        free(all_samples);
        g_pcm_data = NULL;
        return;
    }

    g_playing = 1;
    waveOutWrite(g_wave_out, &g_wave_hdr, sizeof(g_wave_hdr));
}

void startup_audio_stop(void) {
    if (!g_playing && !g_pcm_data) return;
    if (g_playing) {
        waveOutReset(g_wave_out);
        waveOutUnprepareHeader(g_wave_out, &g_wave_hdr, sizeof(g_wave_hdr));
        waveOutClose(g_wave_out);
        g_playing = 0;
    }
    free(g_pcm_data);
    g_pcm_data = NULL;
    g_pcm_samples = 0;
}

#else

void startup_audio_play(const char *exe_dir) {
    (void)exe_dir;
}

void startup_audio_stop(void) {
}

#endif
