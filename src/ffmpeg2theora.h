#ifndef _F2T_FFMPEG2THEORA_H_
#define _F2T_FFMPEG2THEORA_H_

#include "subtitles.h"

typedef struct ff2theora_subtitle{
    char *text;
    size_t len;
    double t0;
    double t1;
#ifdef HAVE_KATE
    kate_region kr;
    kate_palette kp;
    kate_bitmap kb;
#endif
    int x1,x2,y1,y2;
} ff2theora_subtitle;

typedef struct ff2theora_kate_stream{
    /* this block valid for subtitles loaded from a file */
    const char *filename;
    size_t num_subtitles;
    ff2theora_subtitle *subtitles;

    /* this block valid for subtitles coming from the source video */
    int stream_index;

    /* this block valid for all subtitle sources */
    size_t subtitles_count; /* total subtitles output so far */
    char *subtitles_encoding;
    char subtitles_language[16];
    char subtitles_category[16];
} ff2theora_kate_stream;

typedef struct ff2theora{
    AVFormatContext *context;
    int video_index;
    int audio_index;

    int deinterlace;
    int soft_target;
    int buf_delay;
    int vhook;
    int disable_video;
    int no_upscaling;

    int audiostream;
    int sample_rate;
    int channels;
    int disable_audio;
    float audio_quality;
    int audio_bitrate;
    int preset;

    int included_subtitles;
    int disable_metadata;
    int disable_oshash;

    int videostream;
    int picture_width;
    int picture_height;
    double fps;
    struct SwsContext *sws_colorspace_ctx; /* for image resampling/resizing */
    struct SwsContext *sws_scale_ctx; /* for image resampling/resizing */
    ogg_int32_t aspect_numerator;
    ogg_int32_t aspect_denominator;
    int colorspace;
    AVRational pixel_aspect;
    AVRational frame_aspect;
    int max_x;
    int max_y;

    int pix_fmt;
    int video_quality;
    int video_bitrate;
    ogg_uint32_t keyint;
    char pp_mode[255];
    int resize_method;

    AVRational force_input_fps;
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
    double start_time;
    double end_time;

    AVRational framerate_new;
    AVRational framerate;

    int64_t pts_offset_frame; /* frame, which pts is used as pts_offset */
    int64_t pts_offset; /* base value for input pts */
    int64_t frame_count; /* total video frames output so far */
    int64_t sample_count; /* total audio samples output so far */

    size_t n_kate_streams;
    ff2theora_kate_stream *kate_streams;

    int ignore_non_utf8;
    // ffmpeg2theora --nosound -f dv -H 32000 -S 0 -v 8 -x 384 -y 288 -G 1.5 input.dv
    double video_gamma;
    double video_bright;
    double video_contr;
    double video_satur;
    int y_lut_used;
    int uv_lut_used;
    unsigned char y_lut[256];
    unsigned char uv_lut[256];

}
*ff2theora;

#endif
