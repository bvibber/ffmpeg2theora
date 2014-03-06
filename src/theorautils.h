/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * theorautils.h -- Ogg Theora/Ogg Vorbis Abstraction and Muxing
 * Copyright (C) 2003-2009 <j@v2v.cc>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with This program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _F2T_THEORAUTILS_H_
#define _F2T_THEORAUTILS_H_

#include <stdint.h>
#include <time.h>
#include "theora/theoraenc.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"
#ifdef HAVE_KATE
#include "kate/kate.h"
#endif
#include "ogg/ogg.h"
#include "index.h"

//#define OGGMUX_DEBUG

#define FISHEAD_IDENTIFIER "fishead\0"
#define FISBONE_IDENTIFIER "fisbone\0"
#define INDEX_IDENTIFIER "index\0"
#define FISBONE_SIZE 52
#define FISBONE_MESSAGE_HEADER_OFFSET 44
#define KEYPOINT_SIZE 20
#define SKELETON_VERSION(major, minor) (((major)<<16)|(minor))

typedef struct
{
#ifdef HAVE_KATE
    kate_state k;
    kate_info ki;
    kate_comment kc;
#endif
    ogg_stream_state ko;    /* take physical pages, weld into a logical
                             * stream of packets */
    int katepage_valid;
    unsigned char *katepage;
    int katepage_len;
    int katepage_buffer_length;
    double katetime;
    seek_index index;
    ogg_int64_t last_end_time;
}
oggmux_kate_stream;

enum SeekableState {
    MAYBE_SEEKABLE = -1,
    NOT_SEEKABLE = 0,
    SEEKABLE = 1,
};

typedef struct
{
    /* the file the mixed ogg stream is written to */
    FILE *outfile;
    /* Greather than zero if outfile is seekable.
       Value one of SeekableState. */
    int output_seekable;

    char oshash[32];
    int audio_only;
    int video_only;
    int with_skeleton;
    int skeleton_3;
    int index_interval;
    int theora_index_reserve;
    int vorbis_index_reserve;
    int kate_index_reserve;
    int indexing_complete;
    FILE *frontend;
    /* vorbis settings */
    int sample_rate;
    int channels;
    double vorbis_quality;
    int vorbis_bitrate;

    vorbis_info vi;       /* struct that stores all the static vorbis bitstream settings */
    vorbis_comment vc;    /* struct that stores all the user comments */

    /* theora settings */
    th_info ti;
    th_comment tc;
    int speed_level;

    /* state info */
    th_enc_ctx *td;
    vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
    vorbis_block vb;     /* local working space for packet->PCM decode */

    int with_kate;

    /* used for muxing */
    ogg_stream_state to;    /* take physical pages, weld into a logical
                             * stream of packets */
    ogg_stream_state vo;    /* take physical pages, weld into a logical
                             * stream of packets */
    ogg_stream_state so;    /* take physical pages, weld into a logical
                             * stream of packets, used for skeleton stream */

    int audiopage_valid;
    int videopage_valid;
    unsigned char *audiopage;
    unsigned char *videopage;
    int videopage_len;
    int audiopage_len;
    int videopage_buffer_length;
    int audiopage_buffer_length;

    /* some stats */
    double audiotime;
    double videotime;
    double duration;

    int vkbps;
    int akbps;
    ogg_int64_t audio_bytesout;
    ogg_int64_t video_bytesout;
    ogg_int64_t kate_bytesout;
    time_t start_time;

    //to do some manual page flusing
    int v_pkg;
    int a_pkg;
    int k_pkg;
#ifdef OGGMUX_DEBUG
    int a_page;
    int v_page;
    int k_page;
#endif

    FILE *twopass_file;
    int twopass;
    int passno;

    int n_kate_streams;
    oggmux_kate_stream *kate_streams;

    seek_index theora_index;
    seek_index vorbis_index;
    int prev_vorbis_window; /* Window size of previous vorbis block. Used to
                               calculate duration of vorbis packets. */
    /* The offset of the first non header page in bytes. */
    ogg_int64_t content_offset;
    /* Granulepos of the last encoded packet. */
    ogg_int64_t vorbis_granulepos;

    ogg_int32_t serialno;
}
oggmux_info;

void init_info(oggmux_info *info);
extern void oggmux_setup_kate_streams(oggmux_info *info, int n_kate_streams);
extern void oggmux_init (oggmux_info *info);
extern void oggmux_add_video (oggmux_info *info, th_ycbcr_buffer ycbcr, int e_o_s);
extern void oggmux_add_audio (oggmux_info *info, uint8_t **buffer, int samples,int e_o_s);
#ifdef HAVE_KATE
extern void oggmux_add_kate_text (oggmux_info *info, int idx, double t0, double t1, const char *text, size_t len, int x1, int x2, int y1, int y2);
extern void oggmux_add_kate_image (oggmux_info *info, int idx, double t0, double t1, const kate_region *kr, const kate_palette *kp, const kate_bitmap *kb);
extern void oggmux_add_kate_end_packet (oggmux_info *info, int idx, double t);
#endif
extern void oggmux_flush (oggmux_info *info, int e_o_s);
extern void oggmux_close (oggmux_info *info);

extern int write_seek_index (oggmux_info* info);


#endif
