/* Sample-format conversion for audiod's RT loop (plan.md C16). hw_params
 * negotiates one of these per PCM; audiod maps the snd_pcm_format_t to this
 * enum once per lock, so no ALSA header is needed here.
 *
 * Both PCMs are 2-channel interleaved. Capture is L = Source, R = System;
 * playback is the processed mono signal on both L and R. Full scale is
 * [-1, 1): an n-bit sample is scaled by 2^-(n-1).
 *
 * Each call switches on the format once and runs the per-sample loop inside
 * that case. No allocation, no syscalls. */
#ifndef PCMFMT_H
#define PCMFMT_H

#include <stddef.h>

enum pcm_fmt {
    PCM_FMT_S32_LE,  /* 4 bytes */
    PCM_FMT_S24_3LE, /* 3 bytes, packed */
    PCM_FMT_S24_LE,  /* low 3 bytes of a 4-byte word */
};

/* Bytes per sample (one channel), or 0 for a value outside the enum. */
size_t pcm_fmt_bytes(enum pcm_fmt f);

/* raw: frames of interleaved L/R in format f. source gets L as float64 (the
 * EQ input); ring gets L, R interleaved as float32 (the raw ring push). An
 * S24_LE word's top byte is ignored. */
void pcm_capture(enum pcm_fmt f, const void *raw, size_t frames,
                 double *source, float *ring);

/* Writes mono to both channels of raw in format f, rounded to nearest and
 * clamped to the format's rails. A non-finite sample (NaN or +-Inf) is
 * written as 0. Returns the number of non-finite samples, so the caller can
 * re-mute that period (C11). */
size_t pcm_playback(enum pcm_fmt f, const double *mono, size_t frames, void *raw);

#endif
