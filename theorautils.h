/*
 * theorautils.h -- ogg/theora Utils for ffmpeg2ogg
 * Copyright (C) 2003 <j@v2v.cc>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdint.h>
#include "theora/theora.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"
#include "ogg/ogg.h"

#define V2V_PRESET_PRO 1
#define V2V_PRESET_PREVIEW 2

typedef struct ff2theora{
    AVFormatContext *context;
    int video_index;
    int audio_index;
    
    int deinterlace;
    int audiostream;
    int sample_rate;
    int channels;
    int disable_audio;
    float audio_quality;
    int audio_bitrate;
    
    int picture_width;
    int picture_height;
    double fps;
    ImgReSampleContext *img_resample_ctx; /* for image resampling/resizing */
    ReSampleContext *audio_resample_ctx;
    ogg_int32_t aspect_numerator;
    ogg_int32_t aspect_denominator;
    double    frame_aspect;

    int video_quality;
    int video_bitrate;
    int sharpness;
    int keyint;

    double force_input_fps;
    int sync;
    
    /* cropping */
    int frame_topBand;
    int frame_bottomBand;
    int frame_leftBand;
    int frame_rightBand;
    
    int frame_width;
    int frame_height;
    int frame_x_offset;
    int frame_y_offset;

    /* In seconds */
    int start_time;
    int end_time; 

    double pts_offset; /* between given input pts and calculated output pts */
    int64_t frame_count; /* total video frames output so far */
    int64_t sample_count; /* total audio samples output so far */
}
*ff2theora;



typedef struct
{
    int debug;
    int preset;
    int audio_only;
    int video_only;
    int sample_rate;
    int channels;
    double vorbis_quality;
    int vorbis_bitrate;
    int scale;
    int wide;
    double fps;
    double start;
    double end;
    double audiotime;
    double videotime;
    int vkbps;
    int akbps;
    ogg_int64_t audio_bytesout;
    ogg_int64_t video_bytesout;

    ogg_stream_state to;    /* take physical pages, weld into a logical
                             * stream of packets */
    ogg_stream_state vo;    /* take physical pages, weld into a logical
                             * stream of packets */
    ogg_packet op;  /* one raw packet of data for decode */

    theora_info ti;
    theora_comment tc;
    
    theora_state td;


    vorbis_info vi;       /* struct that stores all the static vorbis bitstream settings */
    vorbis_comment vc;    /* struct that stores all the user comments */

    vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
    vorbis_block vb;     /* local working space for packet->PCM decode */
    FILE *outfile;

    int audiopage_valid;
    int videopage_valid;
    unsigned char *audiopage;
    unsigned char *videopage;
    int videopage_len;
    int audiopage_len;
    int videopage_buffer_length;
    int audiopage_buffer_length;
}
theoraframes_info;

extern void init_info(theoraframes_info *info);
extern void theoraframes_init (theoraframes_info *info);
extern int theoraframes_add_video (ff2theora this, theoraframes_info *info, AVFrame *avframe, int e_o_s);
extern int theoraframes_add_audio (theoraframes_info *info, int16_t * readbuffer, int bytesread, int samplesread,int e_o_s);
extern void theoraframes_flush (theoraframes_info *info, int e_o_s);
extern void theoraframes_close (theoraframes_info *info);
