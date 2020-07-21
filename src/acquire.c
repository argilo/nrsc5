/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <string.h>

#include "acquire.h"
#include "defines.h"
#include "input.h"

#define FILTER_DELAY 15

static float filter_taps_fm[] = {
    -0.000685643230099231,
    0.005636964458972216,
    0.009015781804919243,
    -0.015486305579543114,
    -0.035108357667922974,
    0.017446253448724747,
    0.08155813068151474,
    0.007995186373591423,
    -0.13311293721199036,
    -0.0727422907948494,
    0.15914097428321838,
    0.16498781740665436,
    -0.1324498951435089,
    -0.2484012246131897,
    0.051773931831121445,
    0.2821577787399292,
    0.051773931831121445,
    -0.2484012246131897,
    -0.1324498951435089,
    0.16498781740665436,
    0.15914097428321838,
    -0.0727422907948494,
    -0.13311293721199036,
    0.007995186373591423,
    0.08155813068151474,
    0.017446253448724747,
    -0.035108357667922974,
    -0.015486305579543114,
    0.009015781804919243,
    0.005636964458972216,
    -0.000685643230099231,
    0
};

static float filter_taps_am[] = {
    -0.00038464731187559664,
    -0.00021618751634377986,
    0.0026779419276863337,
    -0.00029802651260979474,
    -0.0012626448879018426,
    -0.0013182522961869836,
    -0.012252614833414555,
    0.015980124473571777,
    0.037112727761268616,
    -0.05451361835002899,
    -0.05804193392395973,
    0.11320608854293823,
    0.055298302322626114,
    -0.16878043115139008,
    -0.022917453199625015,
    0.19178225100040436,
    -0.022917453199625015,
    -0.16878043115139008,
    0.055298302322626114,
    0.11320608854293823,
    -0.05804193392395973,
    -0.05451361835002899,
    0.037112727761268616,
    0.015980124473571777,
    -0.012252614833414555,
    -0.0013182522961869836,
    -0.0012626448879018426,
    -0.00029802651260979474,
    0.0026779419276863337,
    -0.00021618751634377986,
    -0.00038464731187559664,
    0
};

void acquire_process(acquire_t *st)
{
    float complex max_v = 0, phase_increment;
    float angle, angle_diff, angle_factor, max_mag = -1.0f;
    int samperr = 0;
    unsigned int i, j, keep;
    float complex foo, bar, lastbar, angle_error;

    if (st->idx != st->fftcp * (ACQUIRE_SYMBOLS + 1))
        return;

    if (st->input->sync_state == SYNC_STATE_FINE)
    {
        samperr = st->fftcp / 2 + st->input->sync.samperr;
        st->input->sync.samperr = 0;

        angle_diff = -st->input->sync.angle;
        st->input->sync.angle = 0;
        angle = st->prev_angle + angle_diff;
        st->prev_angle = angle;
    }
    else
    {
        cint16_t y;
        for (i = 0; i < st->fftcp * (ACQUIRE_SYMBOLS + 1); i++)
        {
            fir_q15_execute(st->filter, &st->in_buffer[i], &y);
            st->buffer[i] = conjf(cq15_to_cf_conj(y)); // TODO: conjf for AM only
        }

        memset(st->sums, 0, sizeof(float complex) * st->fftcp);
        for (i = 0; i < st->fftcp; ++i)
        {
            for (j = 0; j < ACQUIRE_SYMBOLS; ++j)
                st->sums[i] += st->buffer[i + j * st->fftcp] * conjf(st->buffer[i + j * st->fftcp + st->fft]);
        }

        for (i = 0; i < st->fftcp; ++i)
        {
            float mag;
            float complex v = 0;

            for (j = 0; j < st->cp; ++j)
                v += st->sums[(i + j) % st->fftcp] * st->shape[j] * st->shape[j + st->fft];

            mag = normf(v);
            if (mag > max_mag)
            {
                max_mag = mag;
                max_v = v;
                samperr = (i + st->fftcp - FILTER_DELAY) % st->fftcp;
            }
        }

        angle_diff = cargf(max_v * cexpf(I * -st->prev_angle));
        angle_factor = (st->prev_angle) ? 0.25 : 1.0;
        angle = st->prev_angle + (angle_diff * angle_factor);
        st->prev_angle = angle;
        input_set_sync_state(st->input, SYNC_STATE_COARSE);
    }

    for (i = 0; i < st->fftcp * (ACQUIRE_SYMBOLS + 1); i++)
        st->buffer[i] = cq15_to_cf_conj(st->in_buffer[i]);

    sync_adjust(&st->input->sync, st->fftcp / 2 - samperr);
    angle -= 2 * M_PI * st->cfo;

    st->phase *= cexpf(-(st->fftcp / 2 - samperr) * angle / st->fft * I);

    phase_increment = cexpf(angle / st->fft * I);

    float complex temp_phase = st->phase;
    foo = 0;
    angle_error = 0;
    for (i = 0; i < ACQUIRE_SYMBOLS; ++i)
    {
        int j;
        bar = 0;
        for (j = 0; j < st->fftcp; ++j)
        {
            float complex sample = temp_phase * st->buffer[i * st->fftcp + j + samperr];
            foo += sample;
            bar += sample;
            temp_phase *= phase_increment;
        }
        if (i > 0) {
            angle_error += (bar / lastbar);
        }
        temp_phase /= cabsf(temp_phase);
        lastbar = bar;
    }
    phase_increment *= cexpf((-cargf(angle_error) / st->fftcp) * I);
    st->phase *= cexpf(-(cargf(foo) - cargf(angle_error) * ACQUIRE_SYMBOLS / 2)*I);


    for (i = 0; i < ACQUIRE_SYMBOLS; ++i)
    {
        int j;
        for (j = 0; j < st->fftcp; ++j)
        {
            float complex sample = st->phase * st->buffer[i * st->fftcp + j + samperr];
            if (j < st->cp)
                st->fftin[(j + 128 - 7) % st->fft] = st->shape[j] * sample; // TODO: remove -7 for FM
            else if (j < st->fft)
                st->fftin[(j + 128 - 7) % st->fft] = sample;
            else
                st->fftin[(j + 128 - 7) % st->fft] += st->shape[j] * sample;

            st->phase *= phase_increment;
        }
        st->phase /= cabsf(st->phase);

        fftwf_execute(st->fft_plan);
        fftshift(st->fftout, st->fft);
        //complex float ref = st->fftout[128+1] - conjf(st->fftout[128-1]);
        //int thisbit = (cargf(ref) > 0) ? 1 : 0;
        //printf("%d", thisbit);
        sync_push(&st->input->sync, st->fftout);
    }
    //printf("\n");

    keep = st->fftcp + (st->fftcp / 2 - samperr);
    memmove(&st->in_buffer[0], &st->in_buffer[st->idx - keep], sizeof(cint16_t) * keep);
    st->idx = keep;
}

void acquire_cfo_adjust(acquire_t *st, int cfo)
{
    float hz;

    if (cfo == 0)
        return;

    st->cfo += cfo;
    hz = (float) st->cfo * SAMPLE_RATE / 2 / st->fft; // TODO: does this need to accomodate AM?

    log_info("CFO: %f Hz", hz);
}

unsigned int acquire_push(acquire_t *st, cint16_t *buf, unsigned int length)
{
    unsigned int needed = st->fftcp - st->idx % st->fftcp;

    if (length < needed)
        return 0;

    memcpy(&st->in_buffer[st->idx], buf, sizeof(cint16_t) * needed);
    st->idx += needed;

    return needed;
}

void acquire_reset(acquire_t *st)
{
    firdecim_q15_reset(st->filter);
    st->idx = 0;
    st->prev_angle = 0;
    st->phase = 1;
    st->cfo = 0;
}

void acquire_init(acquire_t *st, input_t *input, int mode)
{
    int i;

    st->fft = (mode == NRSC5_MODE_FM ? FFT_FM : FFT_AM);
    st->fftcp = (mode == NRSC5_MODE_FM ? FFTCP_FM : FFTCP_AM);
    st->cp = (mode == NRSC5_MODE_FM ? CP_FM : CP_AM);

    st->input = input;

    if (mode == NRSC5_MODE_FM)
        st->filter = firdecim_q15_create(filter_taps_fm, sizeof(filter_taps_fm) / sizeof(filter_taps_fm[0]));
    else
        st->filter = firdecim_q15_create(filter_taps_am, sizeof(filter_taps_am) / sizeof(filter_taps_am[0]));

    st->fft_plan = fftwf_plan_dft_1d(st->fft, st->fftin, st->fftout, FFTW_FORWARD, 0);

    for (i = 0; i < st->fftcp; ++i)
    {
        // Pulse shaping window function
        if (i < st->cp)
            st->shape[i] = sinf(M_PI / 2 * i / st->cp);
        else if (i < st->fft)
            st->shape[i] = 1;
        else
            st->shape[i] = cosf(M_PI / 2 * (i - st->fft) / st->cp);
    }

    acquire_reset(st);
}

void acquire_free(acquire_t *st)
{
    firdecim_q15_free(st->filter);
    fftwf_destroy_plan(st->fft_plan);
}
