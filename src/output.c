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

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include "bitreader.h"
#include "bitwriter.h"
#include "defines.h"
#include "output.h"

#ifdef USE_FAAD2
static ao_sample_format sample_format = {
    16,
    44100,
    2,
    AO_FMT_LITTLE,
    "L,R"
};
#endif

void hdc_to_aac(bitreader_t *br, bitwriter_t *bw);

static void write_adts_header(FILE *fp, unsigned int len)
{
    uint8_t hdr[7];
    bitwriter_t bw;

    bw_init(&bw, hdr);
    bw_addbits(&bw, 0xFFF, 12); // sync word
    bw_addbits(&bw, 0, 1); // MPEG-4
    bw_addbits(&bw, 0, 2); // Layer
    bw_addbits(&bw, 1, 1); // no CRC
    bw_addbits(&bw, 1, 2); // AAC-LC
    bw_addbits(&bw, 7, 4); // 22050 HZ
    bw_addbits(&bw, 0, 1); // private bit
    bw_addbits(&bw, 2, 3); // 2-channel configuration
    bw_addbits(&bw, 0, 1);
    bw_addbits(&bw, 0, 1);
    bw_addbits(&bw, 0, 1);
    bw_addbits(&bw, 0, 1);
    bw_addbits(&bw, len + 7, 13); // frame length
    bw_addbits(&bw, 0x7FF, 11); // buffer fullness (VBR)
    bw_addbits(&bw, 0, 2); // 1 AAC frame per ADTS frame

    fwrite(hdr, 7, 1, fp);
}

static void dump_adts(FILE *fp, uint8_t *pkt, unsigned int len)
{
    uint8_t tmp[1024];
    bitreader_t br;
    bitwriter_t bw;

    br_init(&br, pkt, len);
    bw_init(&bw, tmp);
    hdc_to_aac(&br, &bw);
    len = bw_flush(&bw);

    write_adts_header(fp, len);
    fwrite(tmp, len, 1, fp);
    fflush(fp);
}

static void dump_hdc(FILE *fp, uint8_t *pkt, unsigned int len)
{
    write_adts_header(fp, len);
    fwrite(pkt, len, 1, fp);
    fflush(fp);
}

void output_push(output_t *st, uint8_t *pkt, unsigned int len, unsigned int program, unsigned int seq)
{
    if (program != st->program) return;

    st->audio_packets++;
    st->audio_bytes += len;
    if (st->audio_packets >= 32) {
        log_debug("Audio bit rate: %.1f kbps", (float)st->audio_bytes * 8 * 44100 / 2048 / st->audio_packets / 1000);
        st->audio_packets = 0;
        st->audio_bytes = 0;
    }

    if (st->method == OUTPUT_ADTS)
    {
        dump_adts(st->outfp, pkt, len);
        return;
    }
    else if (st->method == OUTPUT_HDC)
    {
        dump_hdc(st->outfp, pkt, len);
        return;
    }

#ifdef USE_FAAD2
    memcpy(st->packet[seq], pkt, len);
    st->packet_len[seq] = len;
#endif
}

void output_set_index(output_t *st, unsigned int program, unsigned int index)
{
    if (program != st->program) return;
    st->audio_index = index;
}

void output_silence(output_t *st)
{
    ao_play(st->dev, (void *)st->silence, AUDIO_FRAME_BYTES);
}

void output_play(output_t *st)
{
    if (st->method == OUTPUT_ADTS || st->method == OUTPUT_HDC)
        return;

#ifdef USE_FAAD2
    void *buffer;
    NeAACDecFrameInfo info;
    static int start_silence = 8;

    if (start_silence > 0)
    {
        if (st->packet_len[st->audio_index] == 0)
            return;
        while (start_silence--)
            output_silence(st);
    }

    if (st->packet_len[st->audio_index] == 0)
    {
        output_silence(st);
    }
    else
    {
        buffer = NeAACDecDecode(st->handle, &info, st->packet[st->audio_index], st->packet_len[st->audio_index]);
        if (info.error > 0)
        {
            log_error("Decode error: %s", NeAACDecGetErrorMessage(info.error));
            output_silence(st);
        }

        if (info.error == 0 && info.samples > 0)
        {
            unsigned int bytes = info.samples * sample_format.bits / 8;
            assert(bytes == AUDIO_FRAME_BYTES);
            ao_play(st->dev, buffer, AUDIO_FRAME_BYTES);
        }
    }

    st->packet_len[st->audio_index++] = 0;
    st->audio_index &= 0x3f;
#endif
}

void output_reset(output_t *st)
{
    memset(st->ports, 0, sizeof(st->ports));
    st->audio_packets = 0;
    st->audio_bytes = 0;
    st->audio_index = 0;

#ifdef USE_FAAD2
    if (st->method == OUTPUT_ADTS || st->method == OUTPUT_HDC)
        return;

    if (st->handle)
        NeAACDecClose(st->handle);

    unsigned long samprate = 22050;
    NeAACDecInitHDC(&st->handle, &samprate);
    memset(st->silence, 0, sizeof(st->silence));
#endif
}

void output_init_adts(output_t *st, const char *name)
{
    st->method = OUTPUT_ADTS;

    if (strcmp(name, "-") == 0)
        st->outfp = stdout;
    else
        st->outfp = fopen(name, "wb");
    if (st->outfp == NULL)
        FATAL_EXIT("Unable to open output adts file.");
}

void output_init_hdc(output_t *st, const char *name)
{
    st->method = OUTPUT_HDC;

    if (strcmp(name, "-") == 0)
        st->outfp = stdout;
    else
        st->outfp = fopen(name, "wb");
    if (st->outfp == NULL)
        FATAL_EXIT("Unable to open output adts-hdc file.");

    st->aas_files_path = NULL;
}

#ifdef USE_FAAD2
static void output_init_ao(output_t *st, int driver, const char *name)
{
    unsigned int i;

    if (name)
        st->dev = ao_open_file(driver, name, 1, &sample_format, NULL);
    else
        st->dev = ao_open_live(driver, &sample_format, NULL);
    if (st->dev == NULL)
        FATAL_EXIT("Unable to open output wav file.");

    for (i = 0; i < AUDIO_BUFFER_FRAMES; i++)
        st->packet_len[i] = 0;

    st->handle = NULL;
    output_reset(st);

    st->aas_files_path = NULL;
}

void output_init_wav(output_t *st, const char *name)
{
    st->method = OUTPUT_WAV;

    ao_initialize();
    output_init_ao(st, ao_driver_id("wav"), name);
}

void output_init_live(output_t *st)
{
    st->method = OUTPUT_LIVE;

    ao_initialize();
    output_init_ao(st, ao_default_driver_id(), NULL);
}
#endif

static unsigned int id3_length(uint8_t *buf)
{
    return ((buf[0] & 0x7f) << 21) | ((buf[1] & 0x7f) << 14) | ((buf[2] & 0x7f) << 7) | (buf[3] & 0x7f);
}

static char *id3_text(uint8_t *buf, unsigned int frame_len)
{
    char *text;

    if (frame_len == 0)
    {
        text = (char *) malloc(1);
        text[0] = 0;
        return text;
    }
    else
    {
        text = (char *) malloc(frame_len);
        memcpy(text, buf + 1, frame_len - 1);
        text[frame_len - 1] = 0;
        return text;
    }
}

static void output_id3(uint8_t *buf, unsigned int len)
{
    unsigned int off = 0, id3_len;
    if (len < 10 || memcmp(buf + off, "ID3\x03\x00", 5) || buf[off+5]) return;
    id3_len = id3_length(buf + 6) + 10;
    if (id3_len > len) return;
    off += 10;

    while (off + 10 <= id3_len)
    {
        unsigned int frame_len = id3_length(buf + off + 4);
        if (off + 10 + frame_len > id3_len) return;

        if (memcmp(buf + off, "TIT2", 4) == 0)
        {
            char *title = id3_text(buf + off + 10, frame_len);
            log_debug("Title: %s", title);
            free(title);
        }
        else if (memcmp(buf + off, "TPE1", 4) == 0)
        {
            char *artist = id3_text(buf + off + 10, frame_len);
            log_debug("Artist: %s", artist);
            free(artist);
        }
        else if (memcmp(buf + off, "TALB", 4) == 0)
        {
            char *album = id3_text(buf + off + 10, frame_len);
            log_debug("Album: %s", album);
            free(album);
        }

        off += 10 + frame_len;
    }
}

static void parse_port_info(output_t *st, uint8_t *buf, unsigned int len)
{
    static int dump = 1;
    unsigned int idx = 0;
    uint8_t *p = buf;
    while (p < buf + len)
    {
        uint8_t type = *p++;
        switch (type & 0xF0)
        {
        case 0x40:
        {
            if (dump)
                log_debug("%02X %02X %02X %02X", type, p[0], p[1], p[2]);
            p += 3;
            break;
        }
        case 0x60:
        {
            // length (1-byte) value (length - 1)
            uint8_t l = *p++;
            if (type == 0x69)
            {
                char tmp[l - 1];
                memcpy(tmp, p + 1, l - 2);
                tmp[l - 2] = 0;
                if (dump)
                    log_debug("Found %s", tmp);
            }
            else if (type == 0x67)
            {
                aas_port_t *port = &st->ports[idx++];
                port->port = *(uint16_t*)&p[1];
                port->pkt_size = *(uint16_t*)&p[3];
                port->type = p[5];
                if (dump)
                    log_debug("Port %02X, type %d, size %d", port->port, port->type, port->pkt_size);
            }
            p += l - 1;
            break;
        }
        default:
            if (dump)
                log_warn("unexpected byte %02X", *p);
            goto done;
        }
    }

done:
    // clear unused port structures
    while (idx < MAX_PORTS)
    {
        st->ports[idx++].port = 0;
    }

    // only write to log once (contents should not change often)
    if (dump)
        dump = 0;
}

static void write_file(const char *dirpath, const char *fname, const uint8_t *buf, unsigned int len)
{
#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif
    char fullpath[strlen(dirpath) + strlen(fname) + 2];
    FILE *fp;

    sprintf(fullpath, "%s" PATH_SEPARATOR "%s", dirpath, fname);
    fp = fopen(fullpath, "wb");
    if (fp == NULL)
    {
        log_warn("Failed to open %s (%d)", fullpath, errno);
        return;
    }
    fwrite(buf, 1, len, fp);
    fclose(fp);
}

static aas_port_t *find_port(output_t *st, uint16_t port_id)
{
    unsigned int i;
    for (i = 0; i < MAX_PORTS; i++)
    {
        if (st->ports[i].port == port_id)
            return &st->ports[i];
    }
    return NULL;
}

static void process_port(output_t *st, uint16_t port_id, uint8_t *buf, unsigned int len)
{
    aas_port_t *port = find_port(st, port_id);
    if (port == NULL)
    {
        log_debug("missing port %04X", port_id);
        return;
    }

    switch (port->type)
    {
    case 3: // file
    {
        uint32_t *p32 = (uint32_t *)buf;
        uint32_t seq = p32[1];
        buf += 8;
        len -= 8;
        if (seq == 0)
        {
            uint8_t *p;
            unsigned int namelen;

            p32 = (uint32_t *)buf;
            // unk (4 bytes)
            // unk (4 bytes)
            port->u.file.size = p32[2];
            port->u.file.data = malloc(port->u.file.size);
            port->u.file.type = p32[3];
            log_debug("%08X %08X %08X", p32[0], p32[1], p32[3]);
            buf += 16;
            len -= 16;

            // XXX Filename appears to be deliminated by its extension.
            //     This could be very incorrect.
            p = memchr(buf, '.', len - 16);
            if (p == NULL)
            {
                log_debug("File has invalid name");
                port->u.file.seq = 0;
                break;
            }

            namelen = p - buf + 4;
            free(port->u.file.name);
            port->u.file.name = strndup((const char *)buf, namelen);
            buf += namelen;
            len -= namelen;

            memcpy(port->u.file.data, buf, len);
            port->u.file.idx = len;
            port->u.file.seq = 1;

            log_info("File %s, size %d", port->u.file.name, port->u.file.size);
        }
        else if (seq == port->u.file.seq)
        {
            port->u.file.seq++;
            if (port->u.file.idx + len > port->u.file.size)
            {
                log_info("Port %04X (%d) overflowed", port->port, port->type);
                break;
            }
            memcpy(port->u.file.data + port->u.file.idx, buf, len);
            port->u.file.idx += len;

            if (port->u.file.idx == port->u.file.size)
            {
                log_info("Received %s", port->u.file.name);
                if (st->aas_files_path)
                {
                    write_file(st->aas_files_path, port->u.file.name, port->u.file.data, port->u.file.idx);
                }
            }
        }
        else if (port->u.file.size)
        {
            log_debug("%s expected %d, got %d", port->u.file.name, port->u.file.seq, seq);
        }
        break;
    }
    default:
        break;
    }
}

void output_aas_push(output_t *st, uint8_t *buf, unsigned int len)
{
    uint16_t port = *(uint16_t *)buf;
    uint16_t seq = *(uint16_t *)(buf + 2);
    if (port == 0x5100 || (port >= 0x5201 && port <= 0x5207))
    {
        // PSD ports
        if ((port & 0x7) == st->program)
            output_id3(buf + 4, len - 4);
    }
    else if (port == 0x20)
    {
        // AAS port information
        // FIXME: what is the last byte for?
        parse_port_info(st, buf + 4, len - 5);
    }
    else if (port >= 0x401 && port <= 0x50FF)
    {
        // FIXME: what is the last byte for?
        process_port(st, port, buf + 4, len - 5);
    }
    else
    {
        log_warn("unknown AAS port %x, seq %x, length %d", port, seq, len);
    }
}

void output_set_program(output_t *st, unsigned int program)
{
    st->program = program;
}

void output_set_aas_files_path(output_t *st, const char *path)
{
    free(st->aas_files_path);
    st->aas_files_path = path == NULL ? NULL : strdup(path);
}
