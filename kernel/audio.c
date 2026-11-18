/* SPDX-License-Identifier: Unlicense */
#include "audio.h"
#include "device.h"

static struct audio_device main_audio_dev = {0};
static struct audio_stream active_streams[AUDIO_MAX_STREAMS];
static bool audio_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

bool audio_initialize(void) {
    if (audio_initialized) return true;

    str_copy(main_audio_dev.name, "Intel HD Audio (HDA)", sizeof(main_audio_dev.name));
    main_audio_dev.format.sample_rate = AUDIO_SAMPLE_RATE_44100;
    main_audio_dev.format.channels = 2;
    main_audio_dev.format.bits_per_sample = 16;
    main_audio_dev.master_volume = 80;
    main_audio_dev.muted = false;
    main_audio_dev.active = true;

    for (size_t i = 0; i < AUDIO_MAX_STREAMS; i++) {
        active_streams[i] = (struct audio_stream){0};
    }

    /* Probe PCI audio */
    struct pci_device audio_pci[2];
    size_t count = pci_find_by_class(PCI_CLASS_MULTIMEDIA, 0xFFU, 0xFFU, audio_pci, 2);
    if (count > 0) {
        str_copy(main_audio_dev.name, "PCI Multimedia Audio", sizeof(main_audio_dev.name));
    }

    /* Register in Device Manager */
    struct device dev = {0};
    str_copy(dev.name, "audio0", DEVICE_NAME_MAX);
    dev.bus = BUS_TYPE_PCI;
    dev.class_type = DEV_CLASS_AUDIO;
    dev.status = DEV_STATUS_RUNNING;
    device_register(&dev);

    audio_initialized = true;
    return true;
}

struct audio_device *audio_get_device(void) {
    if (!audio_initialized) audio_initialize();
    return &main_audio_dev;
}

void audio_set_master_volume(uint8_t volume) {
    main_audio_dev.master_volume = (volume > 100) ? 100 : volume;
}

uint8_t audio_get_master_volume(void) {
    return main_audio_dev.master_volume;
}

void audio_set_muted(bool mute) {
    main_audio_dev.muted = mute;
}

bool audio_is_muted(void) {
    return main_audio_dev.muted;
}

int audio_play_stream(const int16_t *samples, size_t count, uint8_t volume) {
    if (samples == NULL || count == 0) return -1;
    for (size_t i = 0; i < AUDIO_MAX_STREAMS; i++) {
        if (!active_streams[i].active) {
            struct audio_stream *s = &active_streams[i];
            s->buffer = (int16_t *)samples;
            s->length_samples = count;
            s->playback_pos = 0;
            s->stream_volume = (volume > 100) ? 100 : volume;
            s->loop = false;
            s->playing = true;
            s->active = true;
            return (int)i;
        }
    }
    return -1;
}

void audio_mix_output(int16_t *out_buffer, size_t samples_to_mix) {
    if (out_buffer == NULL || samples_to_mix == 0) return;

    for (size_t i = 0; i < samples_to_mix; i++) {
        out_buffer[i] = 0;
    }

    if (main_audio_dev.muted || main_audio_dev.master_volume == 0) return;

    uint32_t master_vol = main_audio_dev.master_volume;

    for (size_t s = 0; s < AUDIO_MAX_STREAMS; s++) {
        struct audio_stream *stream = &active_streams[s];
        if (stream->active && stream->playing && stream->buffer != NULL) {
            for (size_t i = 0; i < samples_to_mix; i++) {
                if (stream->playback_pos >= stream->length_samples) {
                    if (stream->loop) {
                        stream->playback_pos = 0;
                    } else {
                        stream->playing = false;
                        stream->active = false;
                        break;
                    }
                }

                int32_t sample = stream->buffer[stream->playback_pos++];
                int32_t scaled = (sample * (int32_t)stream->stream_volume * (int32_t)master_vol) / 10000;
                int32_t accum = (int32_t)out_buffer[i] + scaled;

                /* Clip to 16-bit range */
                if (accum > 32767) accum = 32767;
                if (accum < -32768) accum = -32768;

                out_buffer[i] = (int16_t)accum;
            }
        }
    }
}

static int16_t tone_buffer[1024];

void audio_play_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz == 0 || duration_ms == 0) return;
    uint32_t sample_rate = main_audio_dev.format.sample_rate;
    uint32_t total_samples = (sample_rate * duration_ms) / 1000U;
    if (total_samples > sizeof(tone_buffer) / sizeof(int16_t)) {
        total_samples = sizeof(tone_buffer) / sizeof(int16_t);
    }

    uint32_t half_period = sample_rate / (frequency_hz * 2U);
    if (half_period == 0) half_period = 1;

    for (uint32_t i = 0; i < total_samples; i++) {
        tone_buffer[i] = ((i / half_period) % 2 == 0) ? 16384 : -16384;
    }

    audio_play_stream(tone_buffer, total_samples, 75);
}

/* Self-test */
static int16_t test_mix_buf[64];
static const int16_t test_stream_samples[64] = {
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000,
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000
};

bool audio_self_test(void) {
    if (!audio_initialize()) return false;

    /* Volume and mute controls */
    audio_set_master_volume(100);
    if (audio_get_master_volume() != 100) return false;

    audio_set_muted(true);
    if (!audio_is_muted()) return false;
    audio_set_muted(false);

    /* Play stream */
    int stream_id = audio_play_stream(test_stream_samples, 64, 100);
    if (stream_id < 0) return false;

    /* Mix samples */
    audio_mix_output(test_mix_buf, 64);
    if (test_mix_buf[0] != 1000 || test_mix_buf[1] != 2000) return false;

    /* Tone test */
    audio_play_tone(440, 10); /* 440 Hz concert A for 10ms */

    return true;
}
