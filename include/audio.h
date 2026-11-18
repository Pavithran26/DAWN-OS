/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_AUDIO_H
#define DAWN_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pci.h"

#define AUDIO_MAX_STREAMS 4U
#define AUDIO_SAMPLE_RATE_44100 44100U
#define AUDIO_SAMPLE_RATE_48000 48000U

struct audio_format {
    uint32_t sample_rate;
    uint8_t  channels;
    uint8_t  bits_per_sample;
};

struct audio_device {
    char name[32];
    struct audio_format format;
    uint8_t master_volume; /* 0..100 */
    bool muted;
    bool active;
};

struct audio_stream {
    int16_t *buffer;
    size_t length_samples;
    size_t playback_pos;
    uint8_t stream_volume; /* 0..100 */
    bool loop;
    bool playing;
    bool active;
};

bool audio_initialize(void);
struct audio_device *audio_get_device(void);

/* Mixer Controls */
void audio_set_master_volume(uint8_t volume);
uint8_t audio_get_master_volume(void);
void audio_set_muted(bool mute);
bool audio_is_muted(void);

/* Playback & Tone Generator */
int audio_play_stream(const int16_t *samples, size_t count, uint8_t volume);
void audio_play_tone(uint32_t frequency_hz, uint32_t duration_ms);
void audio_mix_output(int16_t *out_buffer, size_t samples_to_mix);

bool audio_self_test(void);

#endif /* DAWN_AUDIO_H */
