#pragma once

#include <complex.h>
#include <fftw3.h>
#include "firdecim_q15.h"

typedef struct
{
    struct input_t *input;
    firdecim_q15 filter;
    cint16_t in_buffer[FFTCP_FM * (ACQUIRE_SYMBOLS + 1)];
    float complex buffer[FFTCP_FM * (ACQUIRE_SYMBOLS + 1)];
    float complex sums[FFTCP_FM];
    float complex fftin[FFT_FM];
    float complex fftout[FFT_FM];
    float shape[FFTCP_FM];
    fftwf_plan fft_plan;

    unsigned int idx;
    float prev_angle;
    float complex phase;
    int cfo;

    int fft;
    int fftcp;
    int cp;
} acquire_t;

void acquire_process(acquire_t *st);
void acquire_cfo_adjust(acquire_t *st, int cfo);
unsigned int acquire_push(acquire_t *st, cint16_t *buf, unsigned int length);
void acquire_reset(acquire_t *st);
void acquire_init(acquire_t *st, struct input_t *input, int mode);
void acquire_free(acquire_t *st);
