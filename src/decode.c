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

#include <string.h>

#include "conv.h"
#include "decode.h"
#include "input.h"
#include "pids.h"
#include "private.h"

/* 1012s.pdf figure 10-4 */
static int bl_delay[] = { 2, 1, 5 };
static int ml_delay[] = { 11, 6, 7 };
static int bu_delay[] = { 10, 8, 9 };
static int mu_delay[] = { 4, 3, 0 };
static int el_delay[] = { 0, 1 };
static int eu_delay[] = { 2, 3, 5, 4 };

/* 1012s.pdf figure 10-5 */
static int pids_il_delay[] = { 0, 1, 12, 13, 6, 5, 18, 17, 11, 7, 23, 19 };
static int pids_iu_delay[] = { 2, 4, 14, 16, 3, 8, 15, 20, 9, 10, 21, 22 };

static void bit_map(unsigned char matrix[25 * BLKSZ * 8], int b, int k, int bits)
{
    int col = (9*k) % 25;
    int row = (11*col + 16*(k/25) + 11*(k/50)) % 32;
    matrix[25 * (b*BLKSZ + row) + col] |= bits;
}

/*
static void interleaver_ma1(decode_t *st)
{
    for (int i = 0; i < 6000; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            st->bl[DIVERSITY_DELAY + i*3 + j] = p1_g[i*12 + bl_delay[j]];
            st->ml[i*3 + j] = p1_g[i*12 + ml_delay[j]];
            st->bu[DIVERSITY_DELAY + i*3 + j] = p1_g[i*12 + bu_delay[j]];
            st->mu[i*3 + j] = p1_g[i*12 + mu_delay[j]];
        }
        for (int j = 0; j < 2; j++)
        {
            st->el[i*2 + j] = p3_g[i*6 + el_delay[j]];
        }
        for (int j = 0; j < 4; j++)
        {
            st->eu[i*4 + j] = p3_g[i*6 + eu_delay[j]];
        }
    }

    int b, k, p;
    for (int n = 0; n < 18000; n++)
    {
        b = n/2250;
        k = (n + n/750 + 1) % 750;
        p = n % 3;
        bit_map(st->buffer_pl, b, k, st->bl[n] << p);

        b = (3*n + 3) % 8;
        k = (n + n/3000 + 3) % 750;
        p = 3 + (n % 3);
        bit_map(st->buffer_pl, b, k, st->ml[n] << p);

        b = n/2250;
        k = (n + n/750) % 750;
        p = n % 3;
        bit_map(st->buffer_pu, b, k, st->bu[n] << p);

        b = (3*n) % 8;
        k = (n + n/3000 + 2) % 750;
        p = 3 + (n % 3);
        bit_map(st->buffer_pu, b, k, st->mu[n] << p);
    }
    for (int n = 0; n < 12000; n++)
    {
        b = (3*n + n/3000) % 8;
        k = (n + (n/6000)) % 750;
        p = n % 2;
        bit_map(st->buffer_t, b, k, st->el[n] << p);
    }
    for (int n = 0; n < 24000; n++)
    {
        b = (3*n + n/3000 + 2*(n/12000)) % 8;
        k = (n + (n/6000)) % 750;
        p = n % 4;
        bit_map(st->buffer_s, b, k, st->eu[n] << p);
    }

    memmove(st->bl, st->bl + 18000, DIVERSITY_DELAY);
    memmove(st->bu, st->bu + 18000, DIVERSITY_DELAY);
}
*/

// calculate channel bit error rate by re-encoding and comparing to the input
static float calc_cber(int8_t *coded, uint8_t *decoded)
{
    uint8_t r = 0;
    unsigned int i, j, errors = 0;

    // tail biting
    for (i = 0; i < 6; i++)
        r = (r >> 1) | (decoded[P1_FRAME_LEN - 6 + i] << 6);

    for (i = 0, j = 0; i < P1_FRAME_LEN; i++)
    {
        // shift in new bit
        r = (r >> 1) | (decoded[i] << 6);

        if ((coded[j++] > 0) != __builtin_parity(r & 0133))
            errors++;

        if ((coded[j++] > 0) != __builtin_parity(r & 0171))
            errors++;

        if ((j % 6) == 5)
            j++;
        else if ((coded[j++] > 0) != __builtin_parity(r & 0165))
            errors++;
    }

    return (float) errors / P1_FRAME_LEN_ENCODED;
}

static void descramble(uint8_t *buf, unsigned int length)
{
    const unsigned int width = 11;
    unsigned int i, val = 0x3ff;
    for (i = 0; i < length; i += 8)
    {
        unsigned int j;
        for (j = 0; j < 8; ++j)
        {
            int bit = ((val >> 9) ^ val) & 1;
            val |= bit << width;
            val >>= 1;
            buf[i + j] ^= bit;
        }
    }
}

void decode_process_p1(decode_t *st)
{
    const int J = 20, B = 16, C = 36;
    const int8_t v[] = {
        10, 2, 18, 6, 14, 8, 16, 0, 12, 4,
        11, 3, 19, 7, 15, 9, 17, 1, 13, 5
    };
    unsigned int i, out = 0;
    for (i = 0; i < P1_FRAME_LEN_ENCODED; i++)
    {
        int partition = v[i % J];
        int block = ((i / J) + (partition * 7)) % B;
        int k = i / (J * B);
        int row = (k * 11) % 32;
        int column = (k * 11 + k / (32*9)) % C;
        st->viterbi_p1[out++] = st->buffer_pm[(block * 32 + row) * 720 + partition * C + column];
        if ((out % 6) == 5) // depuncture, [1, 1, 1, 1, 1, 0]
            st->viterbi_p1[out++] = 0;
    }

    nrsc5_conv_decode_p1(st->viterbi_p1, st->scrambler_p1);
    nrsc5_report_ber(st->input->radio, calc_cber(st->viterbi_p1, st->scrambler_p1));
    descramble(st->scrambler_p1, P1_FRAME_LEN);
    frame_push(&st->input->frame, st->scrambler_p1, P1_FRAME_LEN);
}

void decode_process_pids(decode_t *st)
{
    const int J = 20, B = 16, C = 36;
    const int8_t v[] = {
        10, 2, 18, 6, 14, 8, 16, 0, 12, 4,
        11, 3, 19, 7, 15, 9, 17, 1, 13, 5
    };
    unsigned int i, out = 0;
    for (i = 0; i < PIDS_FRAME_LEN_ENCODED_FM; i++)
    {
        int partition = v[i % J];
        int block = decode_get_block(st) - 1;
        int k = ((i / J) % (PIDS_FRAME_LEN_ENCODED_FM / J)) + (P1_FRAME_LEN_ENCODED / (J * B));
        int row = (k * 11) % 32;
        int column = (k * 11 + k / (32*9)) % C;
        st->viterbi_pids[out++] = st->buffer_pm[(block * 32 + row) * 720 + partition * C + column];
        if ((out % 6) == 5) // depuncture, [1, 1, 1, 1, 1, 0]
            st->viterbi_pids[out++] = 0;
    }

    nrsc5_conv_decode_pids(st->viterbi_pids, st->scrambler_pids);
    descramble(st->scrambler_pids, PIDS_FRAME_LEN);
    pids_frame_push(&st->pids, st->scrambler_pids);
}

void decode_process_p3(decode_t *st)
{
    const unsigned int J = 4, B = 32, C = 36, M = 2, N = 147456;
    const unsigned int bk_bits = 32 * C;
    const unsigned int bk_adj = 32 * C - 1;
    unsigned int i, out = 0;
    for (i = 0; i < P3_FRAME_LEN_ENCODED; i++)
    {
        int partition = ((st->i_p3 + 2 * (M / 4)) / M) % J;
        unsigned int pti = (st->pt_p3[partition])++;
        int block = (pti + (partition * 7) - (bk_adj * (pti / bk_bits))) % B;
        int row = ((11 * pti) % bk_bits) / C;
        int column = (pti * 11) % C;
        st->viterbi_p3[out++] = st->internal_p3[(block * 32 + row) * 144 + partition * C + column];
        if ((out % 6) == 1 || (out % 6) == 4) // depuncture, [1, 0, 1, 1, 0, 1]
            st->viterbi_p3[out++] = 0;

        st->internal_p3[st->i_p3] = st->buffer_px1[i];
        (st->i_p3)++;
    }
    if (st->ready_p3)
    {
        nrsc5_conv_decode_p3(st->viterbi_p3, st->scrambler_p3);
        descramble(st->scrambler_p3, P3_FRAME_LEN);
        frame_push(&st->input->frame, st->scrambler_p3, P3_FRAME_LEN);
    }
    if (st->i_p3 == N)
    {
        st->i_p3 = 0;
        st->ready_p3 = 1;
    }
}

void decode_process_pids_am(decode_t *st)
{
    uint8_t il[120], iu[120];

    /* 1012s.pdf section 10.4 */
    for (int n = 0; n < 120; n++) {
        int k, p, row;

        p = n % 4;

        k = (n + (n/60) + 11) % 30;
        row = (11 * (k + (k/15)) + 3) % 32;
        // matrix[0][offset + row] |= (il[n] << p);
        il[n] = (st->buffer_pids_am[row*2] >> p) & 1;

        k = (n + (n/60)) % 30;
        row = (11 * (k + (k/15)) + 3) % 32;
        // matrix[1][offset + row] |= (iu[n] << p);
        iu[n] = (st->buffer_pids_am[row*2 + 1] >> p) & 1;
    }

    /* 1012s.pdf figure 10-5 */
    for (int i = 0; i < 10; i++) {
      for (int j = 0; j < 12; j++) {
        // il[i*12 + j] = in[i*24 + pids_il_delay[j]];
        st->viterbi_pids[i*24 + pids_il_delay[j]] = il[i*12 + j] ? 1 : -1;
        // iu[i*12 + j] = in[i*24 + pids_iu_delay[j]];
        st->viterbi_pids[i*24 + pids_iu_delay[j]] = iu[i*12 + j] ? 1 : -1;
      }
    }

    nrsc5_conv_decode_e3(st->viterbi_pids, st->scrambler_pids, PIDS_FRAME_LEN);
    descramble(st->scrambler_pids, PIDS_FRAME_LEN);
    pids_frame_push(&st->pids, st->scrambler_pids);
}

void decode_reset(decode_t *st)
{
    st->idx_pm = 0;
    st->idx_px1 = 0;
    st->i_p3 = 0;
    st->ready_p3 = 0;
    memset(st->pt_p3, 0, sizeof(unsigned int) * 4);
    pids_init(&st->pids, st->input);
}

void decode_init(decode_t *st, struct input_t *input)
{
    st->input = input;
    decode_reset(st);
}
