/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * ffmpeg2theora.c -- Convert ffmpeg supported a/v files to  Ogg Theora / Vorbis
 * Copyright (C) 2003-2011 <j@v2v.cc>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <errno.h>

#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#ifdef HAVE_FRAMEHOOK
#include "libavformat/framehook.h"
#endif
#include "libswscale/swscale.h"
#include "libpostproc/postprocess.h"

#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/samplefmt.h"
#include "libswresample_compat.h"

#include "theora/theoraenc.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"

#ifdef WIN32
#include "fcntl.h"
#endif

#include "theorautils.h"
#include "iso639.h"
#include "subtitles.h"
#include "ffmpeg2theora.h"
#include "avinfo.h"

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio


#define LENGTH(x) (sizeof(x) / sizeof(*x))

enum {
    NULL_FLAG,
    DEINTERLACE_FLAG,
    NODEINTERLACE_FLAG,
    SOFTTARGET_FLAG,
    TWOPASS_FLAG,
    FIRSTPASS_FLAG,
    SECONDPASS_FLAG,
    OPTIMIZE_FLAG,
    NOSYNC_FLAG,
    NOAUDIO_FLAG,
    NOVIDEO_FLAG,
    NOSUBTITLES_FLAG,
    SUBTITLETYPES_FLAG,
    NOMETADATA_FLAG,
    NOOSHASH_FLAG,
    NOUPSCALING_FLAG,
    CROPTOP_FLAG,
    CROPBOTTOM_FLAG,
    CROPRIGHT_FLAG,
    CROPLEFT_FLAG,
    ASPECT_FLAG,
    PIXEL_ASPECT_FLAG,
    MAXSIZE_FLAG,
    INPUTFPS_FLAG,
    AUDIOSTREAM_FLAG,
    VIDEOSTREAM_FLAG,
    SUBTITLES_FLAG,
    SUBTITLES_ENCODING_FLAG,
    SUBTITLES_LANGUAGE_FLAG,
    SUBTITLES_CATEGORY_FLAG,
    SUBTITLES_IGNORE_NON_UTF8_FLAG,
    VHOOK_FLAG,
    FRONTEND_FLAG,
    FRONTENDFILE_FLAG,
    SPEEDLEVEL_FLAG,
    PP_FLAG,
    RESIZE_METHOD_FLAG,
    NOSKELETON,
    SKELETON_3,
    INDEX_INTERVAL,
    THEORA_INDEX_RESERVE,
    VORBIS_INDEX_RESERVE,
    KATE_INDEX_RESERVE,
    INFO_FLAG
} F2T_FLAGS;

enum {
    V2V_PRESET_NONE,
    V2V_PRESET_PRO,
    V2V_PRESET_PREVIEW,
    V2V_PRESET_VIDEOBIN,
    V2V_PRESET_PADMA,
    V2V_PRESET_PADMASTREAM,
} F2T_PRESETS;


#define PAL_HALF_WIDTH 384
#define PAL_HALF_HEIGHT 288
#define NTSC_HALF_WIDTH 320
#define NTSC_HALF_HEIGHT 240

#define PAL_FULL_WIDTH 720
#define PAL_FULL_HEIGHT 576
#define NTSC_FULL_WIDTH 720
#define NTSC_FULL_HEIGHT 480

#define INCSUB_TEXT 1
#define INCSUB_SPU 2

oggmux_info info;

static int using_stdin = 0;

static int padcolor[3] = { 16, 128, 128 };


static int ilog(unsigned _v){
  int ret;
  for(ret=0;_v;ret++)_v>>=1;
  return ret;
}

/**
 * Allocate and initialise an AVFrame.
 */
static AVFrame *frame_alloc(int pix_fmt, int width, int height) {
    AVFrame *picture;
    uint8_t *picture_buf;
    int size;

    picture = avcodec_alloc_frame();
    if (!picture)
        return NULL;
    size = avpicture_get_size (pix_fmt, width, height);
    picture_buf = av_malloc (size);
    if (!picture_buf) {
        av_free (picture);
        return NULL;
    }
    avpicture_fill((AVPicture *) picture, picture_buf, pix_fmt, width, height);
    return picture;
}

/**
 * Frees an AVFrame.
 */
static void frame_dealloc(AVFrame *frame) {
    if (frame) {
        avpicture_free((AVPicture*)frame);
        av_free(frame);
    }
}

/**
 * initialize ff2theora with default values
 * @return ff2theora struct
 */
static ff2theora ff2theora_init() {
    ff2theora this = calloc (1, sizeof (*this));
    if (this != NULL) {
        this->disable_audio=0;
        this->disable_video=0;
        this->included_subtitles=INCSUB_TEXT;
        this->disable_metadata=0;
        this->disable_oshash=0;
        this->no_upscaling=0;
        this->video_index = -1;
        this->audio_index = -1;
        this->start_time=0;
        this->end_time=0; /* 0 denotes no end time set */

        // audio
        this->sample_rate = -1;  // samplerate hmhmhm
        this->channels = -1;
        this->audio_quality = 1.00;// audio quality 1
        this->audio_bitrate=0;
        this->audiostream = -1;

        // video
        this->videostream = -1;
        this->picture_width=0;      // set to 0 to not resize the output
        this->picture_height=0;      // set to 0 to not resize the output
        this->video_quality=-1; // defaults set later
        this->video_bitrate=0;
        this->keyint=0;
        this->force_input_fps.num = -1;
        this->force_input_fps.den = 1;
        this->sync = 1;
        this->aspect_numerator=0;
        this->aspect_denominator=0;
        this->colorspace = TH_CS_UNSPECIFIED;
        this->frame_aspect.num=0;
        this->frame_aspect.den=1;
        this->pixel_aspect.num=0;
        this->pixel_aspect.den=0;
        this->max_x=-1;
        this->max_y=-1;
        this->deinterlace=0; // auto by default, if input is flaged as interlaced it will deinterlace.
        this->soft_target=0;
        this->buf_delay=-1;
        this->vhook=0;
        this->framerate_new.num = -1;
        this->framerate_new.den = 1;

        this->frame_topBand=0;
        this->frame_bottomBand=0;
        this->frame_leftBand=0;
        this->frame_rightBand=0;

        this->n_kate_streams=0;
        this->kate_streams=NULL;
        this->ignore_non_utf8 = 0;

        this->pix_fmt = PIX_FMT_YUV420P;

        // ffmpeg2theora --nosound -f dv -H 32000 -S 0 -v 8 -x 384 -y 288 -G 1.5 input.dv
        this->video_gamma  = 0.0;
        this->video_bright = 0.0;
        this->video_contr  = 0.0;
        this->video_satur  = 1.0;

        this->y_lut_used = 0;
        this->uv_lut_used = 0;
        this->sws_colorspace_ctx = NULL;
        this->sws_scale_ctx = NULL;

        this->resize_method = -1;
    }
    return this;
}

// gamma lookup table code

static void y_lut_init(ff2theora this) {
    int i;
    double v;

    double c = this->video_contr;
    double b = this->video_bright;
    double g = this->video_gamma;

    if ((g < 0.01) || (g > 100.0)) g = 1.0;
    if ((c < 0.01) || (c > 100.0)) c = 1.0;
    if ((b < -1.0) || (b > 1.0))   b = 0.0;

    if (g == 1.0 && c == 1.0 && b == 0.0) return;
    this->y_lut_used = 1;

    fprintf(stderr, "  Video correction: gamma=%g, contrast=%g, brightness=%g\n", g, c, b);

    g = 1.0 / g;    // larger values shall make brighter video.

    for (i = 0; i < 256; i++) {
        v = (double) i / 255.0;
        v = c * v + b * 0.1;
        if (v < 0.0) v = 0.0;
        v = pow(v, g) * 255.0;    // mplayer's vf_eq2.c multiplies with 256 here, strange...

        if (v >= 255)
            this->y_lut[i] = 255;
        else
            this->y_lut[i] = (unsigned char)(v+0.5);
    }
}


static void uv_lut_init(ff2theora this) {
    int i;
    double v, s;
    s = this->video_satur;

    if ((s < 0.0) || (s > 100.0)) s = 1.0;

    if (s == 1.0) return;
    this->uv_lut_used = 1;

    fprintf(stderr, "  Color correction: saturation=%g\n", s);

    for (i = 0; i < 256; i++) {
        v = 127.0 + (s * ((double)i - 127.0));
        if (v < 0.0) v = 0.0;

        if (v >= 255.0)
            this->uv_lut[i] = 255;
        else
            this->uv_lut[i] = (unsigned char)(v+0.5);
    }
}

static void lut_init(ff2theora this) {
    y_lut_init(this);
    uv_lut_init(this);
}

static void lut_apply(unsigned char *lut, unsigned char *src, unsigned char *dst, int width, int height, int stride) {
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            dst[x] = lut[src[x]];
        }
        src += stride;
        dst += stride;
    }
}

static void prepare_ycbcr_buffer(ff2theora this, th_ycbcr_buffer ycbcr, AVFrame *frame) {
    /* pysical pages */
    ycbcr[0].width = this->frame_width;
    ycbcr[0].height = this->frame_height;
    ycbcr[0].stride = frame->linesize[0];
    ycbcr[0].data = frame->data[0];

    ycbcr[1].width = this->frame_width / 2;
    ycbcr[1].height = this->frame_height / 2;
    ycbcr[1].stride = frame->linesize[1];
    ycbcr[1].data = frame->data[1];

    ycbcr[2].width = this->frame_width / 2;
    ycbcr[2].height = this->frame_height / 2;
    ycbcr[2].stride = frame->linesize[1];
    ycbcr[2].data = frame->data[2];

    if (this->y_lut_used) {
        lut_apply(this->y_lut, ycbcr[0].data, ycbcr[0].data, ycbcr[0].width, ycbcr[0].height, ycbcr[0].stride);
    }
    if (this->uv_lut_used) {
        lut_apply(this->uv_lut, ycbcr[1].data, ycbcr[1].data, ycbcr[1].width, ycbcr[1].height, ycbcr[1].stride);
        lut_apply(this->uv_lut, ycbcr[2].data, ycbcr[2].data, ycbcr[2].width, ycbcr[2].height, ycbcr[2].stride);
    }
}

static const char *find_category_for_subtitle_stream (ff2theora this, int idx, int included_subtitles)
{
  AVCodecContext *enc = this->context->streams[idx]->codec;
  if (enc->codec_type != AVMEDIA_TYPE_SUBTITLE) return 0;
  switch (enc->codec_id) {
    case AV_CODEC_ID_TEXT:
    case AV_CODEC_ID_SSA:
    case AV_CODEC_ID_MOV_TEXT:
      if (included_subtitles & INCSUB_TEXT)
        return "SUB";
      else
        return NULL;
    case AV_CODEC_ID_DVD_SUBTITLE:
      if (included_subtitles & INCSUB_SPU)
        return "K-SPU";
      else
        return NULL;
    default:
      return NULL;
  }
  return NULL;
}

static int is_supported_subtitle_stream(ff2theora this, int idx, int included_subtitles)
{
  return find_category_for_subtitle_stream(this, idx, included_subtitles) != NULL;
}

static char *get_raw_text_from_ssa(const char *ssa)
{
  int n,intag,inescape;
  char *multiblock = NULL, *realloced_mb;
  char *allocated;
  const char *dialogue, *ptr, *tag_start;

  if (!ssa) return NULL;

  /* turns out some SSA packets have several blocks, each on a single line, so loop */
  while (ssa) {
    dialogue=strstr(ssa, "Dialogue:");
    if (!dialogue) break;

    ptr = dialogue;
    for (n=0;n<9;++n) {
      ptr=strchr(ptr,',');
      if (!ptr) return NULL;
      ++ptr;
    }
    dialogue = ptr;
    allocated = strdup(dialogue);

    /* find all "{...}" tags - the following must work for UTF-8 */
    intag=inescape=0;
    n=0;
    for (ptr=dialogue; *ptr && *ptr!='\n'; ++ptr) {
      if (*ptr=='{') {
        if (intag==0) tag_start = ptr;
        ++intag;
      }
      else if (*ptr=='}') {
        --intag;
        if (intag == 0) {
          /* tag parsing - none for now */
        }
      }
      else if (!intag) {
        if (inescape) {
          if (*ptr == 'N' || *ptr == 'n')
            allocated[n++] = '\n';
          else if (*ptr == 'h')
            allocated[n++] = ' ';
          inescape=0;
        }
        else {
          if (*ptr=='\\') {
            inescape=1;
          }
          else {
            allocated[n++]=*ptr;
          }
        }
      }
    }
    allocated[n]=0;

    /* skip over what we read */
    ssa = ptr;

    /* remove any trailing newlines (also \r characters) */
    n = strlen(allocated);
    while (n>0 && (allocated[n-1]=='\n' || allocated[n-1]=='\r'))
      allocated[--n]=0;

    /* add this new block */
    realloced_mb = (char*)realloc(multiblock, (multiblock?strlen(multiblock):0) + strlen(allocated) + 2); /* \n + 0 */
    if (realloced_mb) {
      if (multiblock) strcat(realloced_mb, "\n"); else strcpy(realloced_mb, "");
      strcat(realloced_mb, allocated);
      multiblock = realloced_mb;
      free(allocated);
    }
  }

  return multiblock;
}

static const float get_ssa_time(const char *p)
{
    int hour, min, sec, hsec;

    if(sscanf(p, "%d:%d:%d%*c%d", &hour, &min, &sec, &hsec) != 4)
        return 0;

    min+= 60*hour;
    sec+= 60*min;
    return (float)(sec*100+hsec)/100;
}

static const float get_duration_from_ssa(const char *ssa)
{
  float d = 2.0f;
  double start, end;
  const char *ptr=ssa;

  ptr=strchr(ptr,',');
  if (!ptr) return d;
  ptr++;
  start = get_ssa_time(ptr);
  ptr=strchr(ptr,',');
  if (!ptr) return d;
  ptr++;
  end = get_ssa_time(ptr);

  return end-start;
}

static void extra_info_from_ssa(AVPacket *pkt, const char **utf8, size_t *utf8len, char **allocated_utf8, float *duration)
{
  char *dupe;

  *allocated_utf8 = NULL;
  dupe = malloc(pkt->size+1); // not zero terminated, so make it so
  if (dupe) {
    memcpy(dupe, pkt->data, pkt->size);
    dupe[pkt->size] = 0;
    *duration = get_duration_from_ssa(dupe);
    *allocated_utf8 = get_raw_text_from_ssa(dupe);
    if (*allocated_utf8) {
      if (*allocated_utf8 == dupe) {
        *allocated_utf8 = NULL;
      }
      else {
        *utf8 = *allocated_utf8;
        *utf8len = strlen(*utf8);
      }
    }
    free(dupe);
  }
}

static const char *find_language_for_subtitle_stream(const AVStream *s)
{
  AVDictionaryEntry *language = av_dict_get(s->metadata, "language", NULL, 0);
  const char *lang=find_iso639_1(language->value);
  if (!lang) {
    fprintf(stderr, "WARNING - unrecognized ISO 639-2 language code: %s\n",
                    language->value);
  }
  return lang;
}

void ff2theora_output(ff2theora this) {
    unsigned int i;
    AVCodecContext *aenc = NULL;
    AVCodecContext *venc = NULL;
    int venc_pix_fmt = 0;
    AVStream *astream = NULL;
    AVStream *vstream = NULL;
    AVCodec *acodec = NULL;
    AVCodec *vcodec = NULL;
    pp_mode *ppMode = NULL;
    pp_context *ppContext = NULL;
    int sws_flags = this->resize_method;
    float frame_aspect = 0;
    double fps = 0.0;
    AVRational vstream_fps;
    int display_width = -1, display_height = -1;
    char *subtitles_enabled = (char*)alloca(this->context->nb_streams);
    char *subtitles_opened = (char*)alloca(this->context->nb_streams);
    int synced = this->start_time == 0.0;
    AVRational display_aspect_ratio, sample_aspect_ratio;

    struct SwrContext *swr_ctx;
    uint8_t **dst_audio_data = NULL;
    int dst_linesize;
    int src_nb_samples = 1024, dst_nb_samples, max_dst_nb_samples;

    if (this->audiostream >= 0 && this->context->nb_streams > this->audiostream) {
        AVCodecContext *enc = this->context->streams[this->audiostream]->codec;
        if (enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            this->audio_index = this->audiostream;
            fprintf(stderr, "  Using stream #0.%d as audio input\n",this->audio_index);
        }
        else {
            fprintf(stderr, "  The selected stream is not audio, falling back to automatic selection\n");
        }
    }
    if (this->videostream >= 0 && this->context->nb_streams > this->videostream) {
        AVCodecContext *enc = this->context->streams[this->videostream]->codec;
        if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            this->video_index = this->videostream;
            fprintf(stderr, "  Using stream #0.%d as video input\n",this->video_index);
        }
        else {
            fprintf(stderr, "  The selected stream is not video, falling back to automatic selection\n");
        }
    }

    for (i = 0; i < this->context->nb_streams; i++) {
        AVCodecContext *enc = this->context->streams[i]->codec;
        switch (enc->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                if (this->video_index < 0 && !this->disable_video)
                    this->video_index = i;
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (this->audio_index < 0 && !this->disable_audio)
                    this->audio_index = i;
                break;
            default:
                break;
        }
    }

    if (this->video_index >= 0) {
        vstream = this->context->streams[this->video_index];
        venc = vstream->codec;
        vcodec = avcodec_find_decoder (venc->codec_id);

        display_width = venc->width;
        display_height = venc->height;
        venc_pix_fmt =  venc->pix_fmt;

        if (this->force_input_fps.num > 0)
            vstream_fps = this->force_input_fps;
        else if (vstream->time_base.den && vstream->time_base.num
                                  && av_q2d(vstream->time_base) > 0.001) {
            vstream_fps.num = vstream->time_base.den;
            vstream_fps.den = vstream->time_base.num;
        } else {
            vstream_fps.num = venc->time_base.den;
            vstream_fps.den = venc->time_base.num * venc->ticks_per_frame;
        }
        if (av_q2d(vstream->avg_frame_rate) < av_q2d(vstream_fps)) {
            vstream_fps = vstream->avg_frame_rate;
        }
        this->fps = fps = av_q2d(vstream_fps);

        venc->thread_count = 1;
        if (vcodec == NULL || avcodec_open2 (venc, vcodec, NULL) < 0) {
            this->video_index = -1;
        }
        this->fps = fps;
#if DEBUG
        fprintf(stderr, "FPS1(stream): %f\n", 1/av_q2d(vstream->time_base));
        fprintf(stderr, "FPS2(stream.r_frame_rate): %f\n", av_q2d(vstream->r_frame_rate));
        fprintf(stderr, "FPS3(codec): %f\n", 1/av_q2d(venc->time_base));
        fprintf(stderr, "ticks per frame: %i\n", venc->ticks_per_frame);
        fprintf(stderr, "FPS used: %f\n", fps);
#endif
        if (this->picture_height==0 &&
            (this->frame_leftBand || this->frame_rightBand || this->frame_topBand || this->frame_bottomBand) ) {
            this->picture_height=display_height-
                    this->frame_topBand-this->frame_bottomBand;
        }
        if (this->picture_width==0 &&
            (this->frame_leftBand || this->frame_rightBand || this->frame_topBand || this->frame_bottomBand) ) {
            this->picture_width=display_width-
                    this->frame_leftBand-this->frame_rightBand;
        }

        //set display_aspect_ratio from source
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  venc->width*vstream->sample_aspect_ratio.num,
                  venc->height*vstream->sample_aspect_ratio.den,
                  1024*1024);

        if (vstream->sample_aspect_ratio.num && // default
            av_cmp_q(vstream->sample_aspect_ratio, venc->sample_aspect_ratio)) {
            sample_aspect_ratio = vstream->sample_aspect_ratio;
        } else {
            sample_aspect_ratio = venc->sample_aspect_ratio;
        }
        if (venc->sample_aspect_ratio.num) {
            av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                      venc->width*venc->sample_aspect_ratio.num,
                      venc->height*venc->sample_aspect_ratio.den,
                      1024*1024);
        }

        if (this->preset == V2V_PRESET_PREVIEW) {
            if (abs(this->fps-30)<1 && (display_width!=NTSC_HALF_WIDTH || display_height!=NTSC_HALF_HEIGHT) ) {
                this->picture_width=NTSC_HALF_WIDTH;
                this->picture_height=NTSC_HALF_HEIGHT;
            }
            else {
                this->picture_width=PAL_HALF_WIDTH;
                this->picture_height=PAL_HALF_HEIGHT;
            }
        }
        else if (this->preset == V2V_PRESET_PRO) {
            if (abs(this->fps-30)<1 && (display_width!=NTSC_FULL_WIDTH || display_height!=NTSC_FULL_HEIGHT) ) {
                this->picture_width=NTSC_FULL_WIDTH;
                this->picture_height=NTSC_FULL_HEIGHT;
            }
            else {
                this->picture_width=PAL_FULL_WIDTH;
                this->picture_height=PAL_FULL_HEIGHT;
            }
        }
        else if (this->preset == V2V_PRESET_PADMA) {
            int width=display_width-this->frame_leftBand-this->frame_rightBand;
            int height=display_height-this->frame_topBand-this->frame_bottomBand;
            if (sample_aspect_ratio.den!=0 && sample_aspect_ratio.num!=0) {
                height=((float)sample_aspect_ratio.den/sample_aspect_ratio.num) * height;
                sample_aspect_ratio.den = 1;
                sample_aspect_ratio.num = 1;
            }
            if (this->frame_aspect.num == 0) {
                this->frame_aspect.num = width;
                this->frame_aspect.den = height;
            }
            if (av_q2d(this->frame_aspect) <= 1.5) {
                if (width > 640 || height > 480) {
                    //4:3 640 x 480
                    this->picture_width=640;
                    this->picture_height=480;
                }
                else {
                    this->picture_width=width;
                    this->picture_height=height;
                }
            }
            else {
                if (width > 640 || height > 360) {
                    //16:9 640 x 360
                    this->picture_width=640;
                    this->picture_height=360;
                }
                else {
                    this->picture_width=width;
                    this->picture_height=height;
                }
            }
            this->frame_aspect.num = this->picture_width;
            this->frame_aspect.den = this->picture_height;
        }
        else if (this->preset == V2V_PRESET_PADMASTREAM) {
            int width=display_width-this->frame_leftBand-this->frame_rightBand;
            int height=display_height-this->frame_topBand-this->frame_bottomBand;
            if (sample_aspect_ratio.den!=0 && sample_aspect_ratio.num!=0) {
                height=((float)sample_aspect_ratio.den/sample_aspect_ratio.num) * height;
                sample_aspect_ratio.den = 1;
                sample_aspect_ratio.num = 1;
            }
            if (this->frame_aspect.num == 0) {
                this->frame_aspect.num = width;
                this->frame_aspect.den = height;
            }

            this->picture_width=128;
            this->picture_height=128/av_q2d(this->frame_aspect);

            this->frame_aspect.num = this->picture_width;
            this->frame_aspect.den = this->picture_height;
        }
        else if (this->preset == V2V_PRESET_VIDEOBIN) {
            int width=display_width-this->frame_leftBand-this->frame_rightBand;
            int height=display_height-this->frame_topBand-this->frame_bottomBand;
            if (sample_aspect_ratio.den!=0 && sample_aspect_ratio.num!=0) {
                height=((float)sample_aspect_ratio.den/sample_aspect_ratio.num) * height;
                sample_aspect_ratio.den = 1;
                sample_aspect_ratio.num = 1;
            }
            if ( ((float)width /height) <= 1.5) {
                if (width > 448) {
                    //4:3 448 x 336
                    this->picture_width=448;
                    this->picture_height=336;
                }
                else {
                    this->picture_width=width;
                    this->picture_height=height;
                }
            }
            else {
                if (width > 512) {
                    //16:9 512 x 288
                    this->picture_width=512;
                    this->picture_height=288;
                }
                else {
                    this->picture_width=width;
                    this->picture_height=height;
                }
            }
            this->frame_aspect.num = this->picture_width;
            this->frame_aspect.den = this->picture_height;
        }
        //so frame_aspect is set on the commandline

        if (this->frame_aspect.num != 0) {
            if (this->picture_height) {
                this->aspect_numerator = this->frame_aspect.num*this->picture_height;
                this->aspect_denominator = this->frame_aspect.den*this->picture_width;
            }
            else{
                this->aspect_numerator = this->frame_aspect.num*display_height;
                this->aspect_denominator = this->frame_aspect.den*display_width;
            }
            av_reduce(&this->aspect_numerator,&this->aspect_denominator,
                       this->aspect_numerator,this->aspect_denominator,
                       1024*1024);
            frame_aspect=av_q2d(this->frame_aspect);
        }
        if ((this->picture_width && !this->picture_height) ||
            (this->picture_height && !this->picture_width) ||
            this->max_x > 0) {

            int width = display_width-this->frame_leftBand-this->frame_rightBand;
            int height = display_height-this->frame_topBand-this->frame_bottomBand;
            if (sample_aspect_ratio.den!=0 && sample_aspect_ratio.num!=0) {
                height=((float)sample_aspect_ratio.den/sample_aspect_ratio.num) * height;
                sample_aspect_ratio.den = 1;
                sample_aspect_ratio.num = 1;
            }
            if (this->frame_aspect.num == 0) {
                this->frame_aspect.num = width;
                this->frame_aspect.den = height;
            }

            if (this->picture_width && !this->picture_height) {
                this->picture_height = this->picture_width / av_q2d(this->frame_aspect);
                this->picture_height = this->picture_height + this->picture_height%2;
            }
            else if (this->picture_height && !this->picture_width) {
                this->picture_width = this->picture_height * av_q2d(this->frame_aspect);
                this->picture_width = this->picture_width + this->picture_width%2;
            }

            if (this->max_x > 0) {
                if (width > height &&
                    this->max_x/av_q2d(this->frame_aspect) <= this->max_y) {
                    this->picture_width = this->max_x;
                    this->picture_height = this->max_x / av_q2d(this->frame_aspect);
                    this->picture_height = this->picture_height + this->picture_height%2;
                } else {
                    this->picture_height = this->max_y;
                    this->picture_width = this->max_y * av_q2d(this->frame_aspect);
                    this->picture_width = this->picture_width + this->picture_width%2;
                }
            }
        }

        if (this->no_upscaling) {
            if (this->picture_height && this->picture_height > display_height) {
                this->picture_width = display_height * display_aspect_ratio.num / display_aspect_ratio.den;
                this->picture_height = display_height;
            }
            else if (this->picture_width && this->picture_width > display_width) {
                this->picture_width = display_width;
                this->picture_height = display_width * display_aspect_ratio.den / display_aspect_ratio.num;
            }
            if (this->fps < av_q2d(this->framerate_new))
                this->framerate_new = vstream_fps;
        }

        if (info.twopass!=3 || info.passno==1) {
            if (sample_aspect_ratio.num!=0 && this->frame_aspect.num==0) {

                // just use the ratio from the input
                this->aspect_numerator=sample_aspect_ratio.num;
                this->aspect_denominator=sample_aspect_ratio.den;
                // or we use ratio for the output
                if (this->picture_height) {
                    int width=display_width-this->frame_leftBand-this->frame_rightBand;
                    int height=display_height-this->frame_topBand-this->frame_bottomBand;
                    av_reduce(&this->aspect_numerator,&this->aspect_denominator,
                    vstream->sample_aspect_ratio.num*width*this->picture_height,
                    vstream->sample_aspect_ratio.den*height*this->picture_width,10000);
                    frame_aspect=(float)(this->aspect_numerator*this->picture_width)/
                                    (this->aspect_denominator*this->picture_height);
                }
                else{
                    frame_aspect=(float)(this->aspect_numerator*display_width)/
                                    (this->aspect_denominator*display_height);
                }
            }
        }

        //pixel aspect ratio set, use that
        if (this->pixel_aspect.num>0) {
            this->aspect_numerator = this->pixel_aspect.num;
            this->aspect_denominator = this->pixel_aspect.den;
            if (this->picture_height) {
                frame_aspect=(float)(this->aspect_numerator*this->picture_width)/
                                (this->aspect_denominator*this->picture_height);
            }
            else{
                frame_aspect=(float)(this->aspect_numerator*display_width)/
                                (this->aspect_denominator*display_height);
            }
        }
        if (!(info.twopass==3 && info.passno==2) && !info.frontend && this->aspect_denominator && frame_aspect) {
            fprintf(stderr, "  Pixel Aspect Ratio: %.2f/1 ",(float)this->aspect_numerator/this->aspect_denominator);
            fprintf(stderr, "  Frame Aspect Ratio: %.2f/1\n", frame_aspect);
        }

        if (!(info.twopass==3 && info.passno==2) && !info.frontend &&
            this->deinterlace==1)
            fprintf(stderr, "  Deinterlace: on\n");
        if (!(info.twopass==3 && info.passno==2) && !info.frontend &&
            this->deinterlace==-1)
            fprintf(stderr, "  Deinterlace: off\n");

        if (strcmp(this->pp_mode, "")) {
            ppContext = pp_get_context(display_width, display_height, PP_FORMAT_420);
            ppMode = pp_get_mode_by_name_and_quality(this->pp_mode, PP_QUALITY_MAX);
            if(!(info.twopass==3 && info.passno==2) && !info.frontend)
                fprintf(stderr, "  Postprocessing: %s\n", this->pp_mode);
        }

        if (venc->color_primaries == AVCOL_PRI_BT470M)
            this->colorspace = TH_CS_ITU_REC_470M;
        else if (venc->color_primaries == AVCOL_PRI_BT470BG)
            this->colorspace = TH_CS_ITU_REC_470BG;

        if (!this->picture_width)
            this->picture_width = display_width;
        if (!this->picture_height)
            this->picture_height = display_height;

        /* Theora has a divisible-by-sixteen restriction for the encoded video size */
        /* scale the frame size up to the nearest /16 and calculate offsets */
        this->frame_width = ((this->picture_width + 15) >>4)<<4;
        this->frame_height = ((this->picture_height + 15) >>4)<<4;

        /*Force the offsets to be even so that chroma samples line up like we
           expect.*/
        this->frame_x_offset = (this->frame_width-this->picture_width)>>1&~1;
        this->frame_y_offset = (this->frame_height-this->picture_height)>>1&~1;

        //Bicubic  (best for upscaling),
        if (sws_flags < 0) {
          if(display_width - (this->frame_leftBand + this->frame_rightBand) < this->picture_width ||
             display_height - (this->frame_topBand + this->frame_bottomBand) < this->picture_height) {
             sws_flags = SWS_BICUBIC;
          } else {        //Bilinear (best for downscaling),
             sws_flags = SWS_BILINEAR;
          }
        }

        if (this->frame_width > 0 || this->frame_height > 0) {
            this->sws_colorspace_ctx = sws_getContext(
                            display_width, display_height, venc_pix_fmt,
                            display_width, display_height, this->pix_fmt,
                            sws_flags, NULL, NULL, NULL
            );
            this->sws_scale_ctx = sws_getContext(
                        display_width - (this->frame_leftBand + this->frame_rightBand),
                        display_height - (this->frame_topBand + this->frame_bottomBand),
                        this->pix_fmt,
                        this->picture_width, this->picture_height, this->pix_fmt,
                        sws_flags, NULL, NULL, NULL
            );
            if (!info.frontend && !(info.twopass==3 && info.passno==2)) {
                if (this->frame_topBand || this->frame_bottomBand ||
                    this->frame_leftBand || this->frame_rightBand ||
                    this->picture_width != (display_width-this->frame_leftBand - this->frame_rightBand) ||
                    this->picture_height != (display_height-this->frame_topBand-this->frame_bottomBand))
                    fprintf(stderr, "  Resize: %dx%d", display_width, display_height);
                if (this->frame_topBand || this->frame_bottomBand ||
                    this->frame_leftBand || this->frame_rightBand) {
                    fprintf(stderr, " => %dx%d",
                        display_width-this->frame_leftBand-this->frame_rightBand,
                        display_height-this->frame_topBand-this->frame_bottomBand);
                }
                if (this->picture_width != (display_width-this->frame_leftBand - this->frame_rightBand)
                    || this->picture_height != (display_height-this->frame_topBand-this->frame_bottomBand))
                    fprintf(stderr, " => %dx%d",this->picture_width, this->picture_height);
                fprintf(stderr, "\n");
            }
        }

        lut_init(this);
    }
    if (!(info.twopass==3 && info.passno==2) && !info.frontend && this->framerate_new.num > 0 && av_cmp_q(vstream_fps, this->framerate_new)) {
        fprintf(stderr, "  Resample Framerate: %0.3f => %0.3f\n",
                        this->fps, av_q2d(this->framerate_new));
    }
    if (this->audio_index >= 0) {
        astream = this->context->streams[this->audio_index];
        aenc = this->context->streams[this->audio_index]->codec;
        acodec = avcodec_find_decoder (aenc->codec_id);
        int sample_rate = aenc->sample_rate;
        if (this->channels < 1) {
            this->channels = aenc->channels;
        }
        if (this->sample_rate==-1) {
            this->sample_rate = aenc->sample_rate;
        }

        if (this->no_upscaling) {
            if (this->sample_rate > aenc->sample_rate)
                this->sample_rate = aenc->sample_rate;
            if (this->channels > aenc->channels)
                this->channels = aenc->channels;
        }
        aenc->thread_count = 1;
        if (acodec != NULL && avcodec_open2 (aenc, acodec, NULL) >= 0) {
            if (this->sample_rate != sample_rate
                || this->channels != aenc->channels
                || aenc->sample_fmt != AV_SAMPLE_FMT_FLTP) {
                swr_ctx = swr_alloc();
                /* set options */
                if (aenc->channel_layout) {
                    av_opt_set_int(swr_ctx, "in_channel_layout",    aenc->channel_layout, 0);
                } else {
                    av_opt_set_int(swr_ctx, "in_channel_layout", av_get_default_channel_layout(aenc->channels), 0);
                }
                av_opt_set_int(swr_ctx, "in_sample_rate",       aenc->sample_rate, 0);
                av_opt_set_int(swr_ctx, "in_sample_fmt", aenc->sample_fmt, 0);

                av_opt_set_int(swr_ctx, "out_channel_layout", av_get_default_channel_layout(this->channels), 0);
                av_opt_set_int(swr_ctx, "out_sample_rate",       this->sample_rate, 0);
                av_opt_set_int(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

                /* initialize the resampling context */
                if (swr_init(swr_ctx) < 0) {
                    fprintf(stderr, "Failed to initialize the resampling context\n");
                    exit(1);
                }

                max_dst_nb_samples = dst_nb_samples =
                    av_rescale_rnd(src_nb_samples, this->sample_rate, sample_rate, AV_ROUND_UP);

                if (av_samples_alloc(dst_audio_data, &dst_linesize, this->channels,
                                     dst_nb_samples, AV_SAMPLE_FMT_FLTP, 0) < 0) {
                    fprintf(stderr, "Could not allocate destination samples\n");
                    exit(1);
                }

                if (!info.frontend && this->sample_rate!=sample_rate)
                    fprintf(stderr, "  Resample: %dHz => %dHz\n", sample_rate,this->sample_rate);
                if (!info.frontend && this->channels!=aenc->channels)
                    fprintf(stderr, "  Channels: %d => %d\n",aenc->channels,this->channels);
            }
            else{
                swr_ctx = NULL;
            }
        }
        else{
            this->audio_index = -1;
        }
    }

    if (info.passno != 1) {
      for (i = 0; i < this->context->nb_streams; i++) {
        subtitles_enabled[i] = 0;
        subtitles_opened[i] = 0;
#ifdef HAVE_KATE
        if (this->included_subtitles) {
          AVStream *stream = this->context->streams[i];
          AVCodecContext *enc = stream->codec;
          const char *category;
          if (enc->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            AVCodec *codec = avcodec_find_decoder (enc->codec_id);
            if (codec && avcodec_open2 (enc, codec, NULL) >= 0) {
              subtitles_opened[i] = 1;
            }
            category = find_category_for_subtitle_stream(this, i, this->included_subtitles);
            if (category) {
              subtitles_enabled[i] = 1;
              add_subtitles_stream(this, i, find_language_for_subtitle_stream(stream), category);
            }
            else if(!info.frontend) {
              fprintf(stderr,"Subtitle stream %d, ignored\n", i);
            }
          }
        }
#endif
      }
    }

#ifdef HAVE_KATE
    if (info.passno != 1) {
      for (i=0; i<this->n_kate_streams; ++i) {
        ff2theora_kate_stream *ks=this->kate_streams+i;
        if (ks->stream_index >= 0) {
#ifdef DEBUG
            printf("Muxing Kate stream %d from input stream %d\n",
                i,ks->stream_index);
#endif
            if (this->included_subtitles) {
              info.with_kate=1;
            }
        }
        else if (load_subtitles(ks,this->ignore_non_utf8,info.frontend)>0) {
#ifdef DEBUG
            printf("Muxing Kate stream %d from %s as %s %s\n",
                i,ks->filename,
                ks->subtitles_language[0]?ks->subtitles_language:"<unknown language>",
                ks->subtitles_category[0]?ks->subtitles_category:"SUB");
#endif
        }
        else {
            if (i!=this->n_kate_streams) {
            memmove(this->kate_streams+i,this->kate_streams+i+1,(this->n_kate_streams-i-1)*sizeof(ff2theora_kate_stream));
            --this->n_kate_streams;
            --i;
          }
        }
      }
    }
#endif

    if (info.passno != 1) {
      oggmux_setup_kate_streams(&info, this->n_kate_streams);
    }

    if (this->video_index >= 0 || this->audio_index >= 0) {
        AVFrame *frame=NULL;
        AVFrame *frame_p=NULL;
        AVFrame *output=NULL;
        AVFrame *output_p=NULL;
        AVFrame *output_tmp_p=NULL;
        AVFrame *output_tmp=NULL;
        AVFrame *output_resized_p=NULL;
        AVFrame *output_resized=NULL;
        AVFrame *output_buffered_p=NULL;
        AVFrame *output_buffered=NULL;
        AVFrame *output_cropped_p=NULL;
        AVFrame *output_cropped=NULL;
        AVFrame *output_padded_p=NULL;
        AVFrame *output_padded=NULL;

        AVPacket pkt;
        AVPacket avpkt;
        int len1;
        int got_frame;
        int first = 1;
        int audio_eos = 0, video_eos = 0, audio_done = 0, video_done = 0;
        int ret;
        AVFrame *audio_frame = NULL;
        uint8_t **audio_p = NULL;
        int no_frames;
        int no_samples;

        double framerate_add = 0;

        if (this->video_index >= 0)
            info.audio_only=0;
        else
            info.audio_only=1;

        if (this->audio_index>=0)
            info.video_only=0;
        else
            info.video_only=1;

        if(info.audio_only)
            video_done = 1;
        if(info.video_only || info.passno == 1)
            audio_done = 1;

        if (!info.audio_only) {
            frame_p = frame = frame_alloc(venc_pix_fmt,
                            venc->width,venc->height);
            output_tmp_p = output_tmp = frame_alloc(this->pix_fmt,
                            venc->width, venc->height);
            output_p = output = frame_alloc(this->pix_fmt,
                            venc->width,venc->height);
            output_resized_p = output_resized = frame_alloc(this->pix_fmt,
                            this->picture_width, this->picture_height);
            output_cropped_p = output_cropped = frame_alloc(this->pix_fmt,
                            venc->width-this->frame_leftBand,
                            venc->height-this->frame_topBand);
            output_buffered_p = output_buffered = frame_alloc(this->pix_fmt,
                            this->frame_width, this->frame_height);
            output_padded_p = output_padded = frame_alloc(this->pix_fmt,
                            this->frame_width, this->frame_height);

            /* video settings here */
            /* config file? commandline options? v2v presets? */

            th_info_init(&info.ti);

            //encoded size
            info.ti.frame_width = this->frame_width;
            info.ti.frame_height = this->frame_height;
            //displayed size
            info.ti.pic_width = this->picture_width;
            info.ti.pic_height = this->picture_height;
            info.ti.pic_x = this->frame_x_offset;
            info.ti.pic_y = this->frame_y_offset;
            if (this->framerate_new.num > 0) {
                // new framerate is interger only right now,
                // so denominator is always 1
                this->framerate = this->framerate_new;
            }
            else {
                this->framerate = vstream_fps;
            }
            info.ti.fps_numerator = this->framerate.num;
            info.ti.fps_denominator = this->framerate.den;

            info.ti.aspect_numerator = this->aspect_numerator;
            info.ti.aspect_denominator = this->aspect_denominator;

            info.ti.colorspace = this->colorspace;

            /*Account for the Ogg page overhead.
              This is 1 byte per 255 for lacing values, plus 26 bytes per 4096 bytes for
               the page header, plus approximately 1/2 byte per packet (not accounted for
               here).*/
            info.ti.target_bitrate=(int)(64870*(ogg_int64_t)this->video_bitrate>>16);

            info.ti.quality = this->video_quality;
            info.ti.keyframe_granule_shift = ilog(this->keyint-1);
            info.ti.pixel_fmt = TH_PF_420;

            /* no longer in new encoder api
            info.ti.dropframes_p = 0;
            info.ti.keyframe_auto_p = 1;
            info.ti.keyframe_frequency = this->keyint;
            info.ti.keyframe_frequency_force = this->keyint;
            info.ti.keyframe_data_target_bitrate = info.ti.target_bitrate * 5;
            info.ti.keyframe_auto_threshold = 80;
            info.ti.keyframe_mindistance = 8;
            info.ti.noise_sensitivity = 1;
            // range 0-2, 0 sharp, 2 less sharp,less bandwidth
            info.ti.sharpness = this->sharpness;
            */
            info.td = th_encode_alloc(&info.ti);

            if (info.speed_level >= 0) {
                int max_speed_level;
                th_encode_ctl(info.td, TH_ENCCTL_GET_SPLEVEL_MAX, &max_speed_level, sizeof(int));
                if (info.speed_level > max_speed_level)
                    info.speed_level = max_speed_level;
                th_encode_ctl(info.td, TH_ENCCTL_SET_SPLEVEL, &info.speed_level, sizeof(int));
            }
            /* setting just the granule shift only allows power-of-two keyframe
               spacing.  Set the actual requested spacing. */
            ret = th_encode_ctl(info.td, TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE,
                                &this->keyint, sizeof(this->keyint-1));
            if(ret<0){
                fprintf(stderr,"Could not set keyframe interval to %d.\n",(int)this->keyint);
            }

            if(this->soft_target){
              /* reverse the rate control flags to favor a 'long time' strategy */
              int arg = TH_RATECTL_CAP_UNDERFLOW;
              ret = th_encode_ctl(info.td, TH_ENCCTL_SET_RATE_FLAGS, &arg, sizeof(arg));
              if(ret<0)
                fprintf(stderr, "Could not set encoder flags for --soft-target\n");
                /* Default buffer control is overridden on two-pass */
                if(!info.twopass && this->buf_delay<0){
                    if((this->keyint*7>>1)>5*this->framerate_new.num/this->framerate_new.den)
                        arg = this->keyint*7>>1;
                    else
                        arg = 5*this->framerate_new.num/this->framerate_new.den;
                    ret = th_encode_ctl(info.td, TH_ENCCTL_SET_RATE_BUFFER, &arg,sizeof(arg));
                    if(ret<0)
                        fprintf(stderr, "Could not set rate control buffer for --soft-target\n");
              }
            }
            /* set up two-pass if needed */
            if(info.passno==1){
              unsigned char *buffer;
              int bytes;
              bytes=th_encode_ctl(info.td,TH_ENCCTL_2PASS_OUT,&buffer,sizeof(buffer));
              if(bytes<0){
                fprintf(stderr,"Could not set up the first pass of two-pass mode.\n");
                fprintf(stderr,"Did you remember to specify an estimated bitrate?\n");
                exit(1);
              }
              /*Perform a seek test to ensure we can overwrite this placeholder data at
                 the end; this is better than letting the user sit through a whole
                 encode only to find out their pass 1 file is useless at the end.*/
              if(fseek(info.twopass_file,0,SEEK_SET)<0){
                fprintf(stderr,"Unable to seek in two-pass data file.\n");
                exit(1);
              }
              if(fwrite(buffer,1,bytes,info.twopass_file)<bytes){
                fprintf(stderr,"Unable to write to two-pass data file.\n");
                exit(1);
              }
              fflush(info.twopass_file);
            }
            if(info.passno==2){
              /* enable second pass here, actual data feeding comes later */
              if(th_encode_ctl(info.td,TH_ENCCTL_2PASS_IN,NULL,0)<0){
                fprintf(stderr,"Could not set up the second pass of two-pass mode.\n");
                exit(1);
              }
              if(info.twopass==3){
                info.videotime = 0;
                this->frame_count = 0;
                if(fseek(info.twopass_file,0,SEEK_SET)<0){
                  fprintf(stderr,"Unable to seek in two-pass data file.\n");
                  exit(1);
                }
              }
            }
            if(info.passno!=1 && this->buf_delay >= 0){
                int arg = this->buf_delay;
                ret = th_encode_ctl(info.td, TH_ENCCTL_SET_RATE_BUFFER,
                                    &this->buf_delay, sizeof(this->buf_delay));
                if (this->buf_delay != arg)
                    fprintf(stderr, "Warning: could not set desired buffer delay of %d, using %d instead.\n",
                                    arg, this->buf_delay);
                if(ret < 0){
                    fprintf(stderr, "Warning: could not set desired buffer delay.\n");
                }
            }

        }
        /* audio settings here */
        info.channels = this->channels;
        info.sample_rate = this->sample_rate;
        info.vorbis_quality = this->audio_quality * 0.1;
        info.vorbis_bitrate = this->audio_bitrate;
        /* subtitles */
#ifdef HAVE_KATE
        if (info.passno != 1) {
          for (i=0; i<this->n_kate_streams; ++i) {
            ff2theora_kate_stream *ks = this->kate_streams+i;
            kate_info *ki = &info.kate_streams[i].ki;
            kate_info_init(ki);
            if (ks->stream_index >= 0 || ks->num_subtitles > 0) {
                if (!info.frontend && !ks->subtitles_language[0]) {
                    fprintf(stderr, "WARNING - Subtitles language not set for input file %d\n",i);
                }
                kate_info_set_language(ki, ks->subtitles_language);
                kate_info_set_category(ki, ks->subtitles_category[0]?ks->subtitles_category:"SUB");
                if (ks->stream_index >= 0) {
                    if (this->force_input_fps.num > 0) {
                        ki->gps_numerator = this->force_input_fps.num;    /* fps= numerator/denominator */
                        ki->gps_denominator = this->force_input_fps.den;
                    }
                    else {
                        if (this->framerate_new.num > 0) {
                            // new framerate is interger only right now,
                            // so denominator is always 1
                            ki->gps_numerator = this->framerate_new.num;
                            ki->gps_denominator = this->framerate_new.den;
                        }
                        else {
                            AVStream *stream = this->context->streams[ks->stream_index];
                            if (stream->time_base.num > 0) {
                                ki->gps_numerator = stream->time_base.den;
                                ki->gps_denominator = stream->time_base.num;
                            }
                            else {
                                ki->gps_numerator = vstream_fps.num;
                                ki->gps_denominator = vstream_fps.den;
                            }
                        }
                    }
                }
                else {
                    // SRT files have millisecond timing
                    ki->gps_numerator = 1000;
                    ki->gps_denominator = 1;
                }
                ki->granule_shift = 32;
                if (display_width >= 0)
                    ki->original_canvas_width = display_width;
                if (display_height >= 0)
                    ki->original_canvas_height = display_height;
            }
          }
        }
#endif

        oggmux_init(&info);
        /*seek to start time*/
        if (this->start_time) {
            int64_t timestamp = this->start_time * AV_TIME_BASE;
            /* add the stream start time */
            if (this->context->start_time != AV_NOPTS_VALUE)
                timestamp += this->context->start_time;
            av_seek_frame( this->context, -1, timestamp, AVSEEK_FLAG_BACKWARD);
            /* discard subtitles by their end time, so we still have those that start before the start time,
             but end after it */
            if (info.passno != 1) {
              for (i=0; i<this->n_kate_streams; ++i) {
                ff2theora_kate_stream *ks=this->kate_streams+i;
                while (ks->subtitles_count < ks->num_subtitles && ks->subtitles[ks->subtitles_count].t1 <= this->start_time) {
                    /* printf("skipping subtitle %u\n", ks->subtitles_count); */
                    ks->subtitles_count++;
                }
              }
            }
        }

        if (this->framerate_new.num > 0) {
            double framerate_new = av_q2d(this->framerate_new);
            framerate_add = framerate_new/this->fps;
            //fprintf(stderr, "calculating framerate addition to %f\n",framerate_add);
            this->fps = framerate_new;
        }

        /*check for end time and calculate number of frames to encode*/
        no_frames = this->fps*(this->end_time - this->start_time) - 1;
        no_samples = this->sample_rate * (this->end_time - this->start_time);
        if ((info.audio_only && this->end_time > 0 && no_samples <= 0)
            || (!info.audio_only && this->end_time > 0 && no_frames <= 0)) {
            fprintf(stderr, "End time has to be bigger than start time.\n");
            exit(1);
        }

        av_init_packet(&avpkt);

        /* main decoding loop */
        do{
            ret = av_read_frame(this->context, &pkt);
            avpkt.size = pkt.size;
            avpkt.data = pkt.data;

            if (ret<0) {
                if (!info.video_only)
                    audio_eos = 1;
                if (!info.audio_only)
                    video_eos = 1;
            }
            else {
                /* check for start time */
                if (!synced) {
                    AVStream *stream=this->context->streams[pkt.stream_index];
                    double t = pkt.pts * av_q2d(stream->time_base) - this->start_time;
                    synced = (t >= 0);
                }
                if (!synced) {
                    /*
                      pipe data to decoder, needed to have
                      first frame decodec in case its not a keyframe
                    */
                    if (pkt.stream_index == this->video_index) {
                      avcodec_decode_video2(venc, frame, &got_frame, &pkt);
                    }
                    av_free_packet (&pkt);
                    continue;
                }
            }

            /* check for end time */
            if (no_frames > 0 && this->frame_count == no_frames) {
                video_eos = 1;
            }

            if ((video_eos && !video_done) || (ret >= 0 && pkt.stream_index == this->video_index)) {
                if (avpkt.size == 0 && !first && !video_eos) {
                    //fprintf (stderr, "no frame available\n");
                }
                while(video_eos || avpkt.size > 0) {
                    int dups = 0;
                    static th_ycbcr_buffer ycbcr;
                    len1 = avcodec_decode_video2(venc, frame, &got_frame, &avpkt);
                    if (len1>=0) {
                        if (got_frame) {
                            // this is disabled by default since it does not work
                            // for all input formats the way it should.
                            if (this->sync == 1 && pkt.dts != AV_NOPTS_VALUE) {
                                if (this->pts_offset == AV_NOPTS_VALUE) {
                                    this->pts_offset = pkt.dts;
                                    this->pts_offset_frame = this->frame_count;
                                }

                                double fr = 1/av_q2d(this->framerate);

                                double ivtime = (pkt.dts - this->pts_offset) * av_q2d(vstream->time_base);
                                double ovtime = (this->frame_count - this->pts_offset_frame) / av_q2d(this->framerate);
                                double delta = ivtime - ovtime;

                                /* it should be larger than half a frame to
                                 avoid excessive dropping and duplicating */

                                if (delta < -0.6*fr) {
#ifdef DEBUG
                                    fprintf(stderr, "Frame dropped to maintain sync\n");
#endif
                                    break;
                                }
                                if (delta >= 1.5*fr) {
                                    dups = (int)(0.5+delta*av_q2d(this->framerate)) - 1;
#ifdef DEBUG
                                    fprintf(stderr, "%d duplicate %s added to maintain sync\n", dups, (dups == 1) ? "frame" : "frames");
#endif
                                }
                            }

                            //For audio only files command line option"-e" will not work
                            //as we don't increment frame_count in audio section.

                            if (venc_pix_fmt != this->pix_fmt) {
                                sws_scale(this->sws_colorspace_ctx,
                                (const uint8_t * const*)frame->data, frame->linesize, 0, display_height,
                                output_tmp->data, output_tmp->linesize);
                            }
                            else{
                                av_picture_copy((AVPicture *)output_tmp, (AVPicture *)frame, this->pix_fmt,
                                                display_width, display_height);
                                output_tmp_p=NULL;
                            }
                            if ((this->deinterlace==0 && frame->interlaced_frame) ||
                                this->deinterlace==1) {
                                if (avpicture_deinterlace((AVPicture *)output,(AVPicture *)output_tmp,this->pix_fmt,display_width,display_height)<0) {
                                        fprintf(stderr, "Deinterlace failed.\n");
                                        exit(1);
                                }
                            }
                            else{
                                av_picture_copy((AVPicture *)output, (AVPicture *)output_tmp, this->pix_fmt,
                                                display_width, display_height);
                            }
                            // now output

                            if (ppMode)
                                pp_postprocess((const uint8_t **)output->data, output->linesize,
                                               output->data, output->linesize,
                                               display_width, display_height,
                                               output->qscale_table, output->qstride,
                                               ppMode, ppContext, this->pix_fmt);
#ifdef HAVE_FRAMEHOOK
                            if (this->vhook)
                                frame_hook_process((AVPicture *)output, this->pix_fmt, display_width,display_height, 0);
#endif

                            if (this->frame_topBand || this->frame_leftBand) {
                                if (av_picture_crop((AVPicture *)output_cropped,
                                                  (AVPicture *)output, this->pix_fmt,
                                                  this->frame_topBand, this->frame_leftBand) < 0) {
                                    av_log(NULL, AV_LOG_ERROR, "error cropping picture\n");
                                }
                                output_cropped_p = NULL;
                            } else {
                                output_cropped = output;
                            }
                            if (this->sws_scale_ctx) {
                                sws_scale(this->sws_scale_ctx,
                                    (const uint8_t * const*)output_cropped->data,
                                    output_cropped->linesize, 0,
                                    display_height - (this->frame_topBand + this->frame_bottomBand),
                                    output_resized->data,
                                    output_resized->linesize);
                            }
                            else{
                                output_resized = output_cropped;
                            }
                            if ((this->frame_width!=this->picture_width) || (this->frame_height!=this->picture_height)) {
                                if (av_picture_pad((AVPicture *)output_padded,
                                                 (AVPicture *)output_resized,
                                                 this->frame_height, this->frame_width, this->pix_fmt,
                                                 this->frame_y_offset, this->frame_y_offset,
                                                 this->frame_x_offset, this->frame_x_offset,
                                                 padcolor ) < 0 ) {
                                    av_log(NULL, AV_LOG_ERROR, "error padding frame\n");
                                }
                            } else {
                                output_padded = output_resized;
                            }
                        }
                        avpkt.size -= len1;
                        avpkt.data += len1;
                    }
                    //now output_resized

                    if (!first) {
                        if (got_frame || video_eos) {
                            prepare_ycbcr_buffer(this, ycbcr, output_buffered);
                            if(dups>0) {
                                //this only works if dups < keyint,
                                //see http://theora.org/doc/libtheora-1.1/theoraenc_8h.html#a8bb9b05471c42a09f8684a2583b8a1df
                                if (th_encode_ctl(info.td,TH_ENCCTL_SET_DUP_COUNT,&dups,sizeof(int)) == TH_EINVAL) {
                                    int _dups = dups;
                                    while(_dups--)
                                        oggmux_add_video(&info, ycbcr, video_eos);
                                }
                            }
                            oggmux_add_video(&info, ycbcr, video_eos);
                            if(video_eos) {
                                video_done = 1;
                            }
                            this->frame_count += dups+1;
                            if (info.passno == 1)
                                info.videotime = this->frame_count / av_q2d(this->framerate);
                        }
                    }
                    if (got_frame) {
                        first=0;
                        av_picture_copy((AVPicture *)output_buffered, (AVPicture *)output_padded, this->pix_fmt, this->frame_width, this->frame_height);
                    }
                    if (!got_frame) {
                        break;
                    }
                }
            }
            if (info.passno!=1)
              if ((audio_eos && !audio_done) || (ret >= 0 && pkt.stream_index == this->audio_index)) {
                while((audio_eos && !audio_done) || avpkt.size > 0 ) {
                    int bytes_per_sample = av_get_bytes_per_sample(aenc->sample_fmt);

                    if (avpkt.size > 0) {
                        if (!audio_frame && !(audio_frame = avcodec_alloc_frame())) {
                            fprintf(stderr, "Failed to allocate memory\n");
                            exit(1);
                        }
                        len1 = avcodec_decode_audio4(astream->codec, audio_frame, &got_frame, &avpkt);
                        if (len1 < 0) {
                            /* if error, we skip the frame */
                            break;
                        }
                        /* Some audio decoders decode only part of the packet, and have to be
                         * called again with the remainder of the packet data.
                         * Sample: http://fate-suite.libav.org/lossless-audio/luckynight-partial.shn
                         * Also, some decoders might over-read the packet. */
                        len1 = FFMIN(len1, avpkt.size);
                        if (got_frame) {
                            dst_nb_samples = audio_frame->nb_samples;
                            if (swr_ctx) {
                                dst_nb_samples = av_rescale_rnd(audio_frame->nb_samples,
                                    this->sample_rate, aenc->sample_rate, AV_ROUND_UP);
                                if (dst_nb_samples > max_dst_nb_samples) {
                                    av_free(dst_audio_data[0]);
                                    if (av_samples_alloc(dst_audio_data, &dst_linesize, this->channels,
                                                           dst_nb_samples, AV_SAMPLE_FMT_FLTP, 1) < 0) {
                                        fprintf(stderr, "Error while converting audio\n");
                                        exit(1);
                                    }
                                    max_dst_nb_samples = dst_nb_samples;
                                }
                                if (swr_convert(swr_ctx, dst_audio_data, dst_nb_samples,
                                    (const uint8_t**)audio_frame->extended_data, audio_frame->nb_samples) < 0) {
                                    fprintf(stderr, "Error while converting audio\n");
                                    exit(1);
                                }
                                audio_p = dst_audio_data;
                            } else {
                                audio_p = audio_frame->extended_data;
                            }
                        }
                        avpkt.size -= len1;
                        avpkt.data += len1;
                    }
                    if(got_frame || audio_eos) {
                        if (no_samples > 0 && this->sample_count + dst_nb_samples > no_samples) {
                            audio_eos = 1;
                            dst_nb_samples = no_samples - this->sample_count;
                            if (dst_nb_samples <= 0) {
                                break;
                            }
                        }
                        oggmux_add_audio(&info, audio_p, dst_nb_samples, audio_eos);
                        avcodec_free_frame(&audio_frame);
                        this->sample_count += dst_nb_samples;
                    }
                    if(audio_eos) {
                        audio_done = 1;
                    }

                    if (audio_eos && avpkt.size <= 0) {
                        break;
                    }
                }
            }

            if (info.passno!=1)
            if (this->included_subtitles && subtitles_enabled[pkt.stream_index] && is_supported_subtitle_stream(this, pkt.stream_index, this->included_subtitles)) {
              AVStream *stream=this->context->streams[pkt.stream_index];
              AVCodecContext *enc = stream->codec;
              if (enc) {
                char *allocated_utf8 = NULL;
                const char *utf8 = NULL;
                size_t utf8len = 0;
                float t;
                AVSubtitle sub;
                int got_sub=0;
                int64_t stream_start_time;

                /* work out timing */
                stream_start_time = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
                t = (float)(pkt.pts - stream_start_time) * stream->time_base.num / stream->time_base.den - this->start_time;
                // my test case has 0 duration, how clever of that. I assume it's that old 'ends whenever the next
                // one starts' hack, but it means I don't know in advance what duration it has. Great!
                float duration;
                if (pkt.duration <= 0) {
                  duration = 2.0f;
                }
                else {
                  duration  = (float)pkt.duration * stream->time_base.num / stream->time_base.den;
                }

                /* generic decoding */
                if (enc->codec && avcodec_decode_subtitle2(enc,&sub,&got_sub,&pkt) >= 0) {
                  if (got_sub) {
                    for (i=0; i<sub.num_rects; i++) {
                      const AVSubtitleRect *rect = sub.rects[i];
                      if (!rect) continue;

                      switch (rect->type) {
                        case SUBTITLE_TEXT:
                            if (!utf8) {
                              if (rect->text) {
                                utf8 = rect->text;
                                utf8len = strlen(utf8);
                              }
                            }
                            break;
                        case SUBTITLE_ASS:
                          /* text subtitles, only one for now */
                          if (!utf8) {
                            if (rect->ass) {
                              extra_info_from_ssa(&pkt,&utf8,&utf8len,&allocated_utf8,&duration);
                            }
                            else if (rect->text) {
                              utf8 = rect->text;
                              utf8len = strlen(utf8);
                            }
                          }
                          break;
                        case SUBTITLE_BITMAP:
                          /* image subtitles */
                          add_image_subtitle_for_stream(this->kate_streams, this->n_kate_streams, pkt.stream_index, t, duration, rect, display_width, display_height, info.frontend);
                          break;

                        default:
                          break;
                      }
                    }
                  }
                }
                else if (enc->codec_id == AV_CODEC_ID_TEXT) {
                  utf8 = (const char *)pkt.data;
                  utf8len = pkt.size;
                }
                else if (enc->codec_id == AV_CODEC_ID_SSA) {
                  // SSA has control stuff in there, extract raw text
                  extra_info_from_ssa(&pkt,&utf8,&utf8len,&allocated_utf8,&duration);
                }
                else if (enc->codec_id == AV_CODEC_ID_MOV_TEXT) {
                  utf8 = (const char *)pkt.data;
                  utf8len = pkt.size;
                  if (utf8len >= 2) {
                    const unsigned char *data = (const unsigned char*)pkt.data;
                    unsigned int text_len = (data[0] << 8) | data[1];
                    utf8 += 2;
                    utf8len -= 2;
                    if (text_len < utf8len) {
                      utf8len = text_len;
                    }
                    if (utf8len == 0) utf8 = NULL;
                  }
                  else {
                    utf8 = NULL;
                    utf8len = 0;
                  }
                }
                else {
                  /* TODO: other types */
                }

                /* clip timings after any possible SSA extraction */
                if (t < 0 && t + duration > 0) {
                  duration += t;
                  t = 0;
                }

                /* we have text and timing now, adjust for start time, encode, and cleanup */
                if (utf8 && t >= 0)
                  add_subtitle_for_stream(this->kate_streams, this->n_kate_streams, pkt.stream_index, t, duration, utf8, utf8len, info.frontend);

                if (allocated_utf8) free(allocated_utf8);
                if (got_sub) {
#if 0
                  avcodec_free_subtitle(enc,&sub);
#endif
                }
              }
            }

            /* if we have subtitles starting before then, add it */
            if (info.passno!=1 && info.with_kate) {
                double avtime = info.audio_only ? info.audiotime :
                    info.video_only ? info.videotime :
                    info.audiotime < info.videotime ? info.audiotime : info.videotime;
                for (i=0; i<this->n_kate_streams; ++i) {
                    ff2theora_kate_stream *ks = this->kate_streams+i;
                    if (ks->num_subtitles > 0) {
                        ff2theora_subtitle *sub = ks->subtitles+ks->subtitles_count;
                        /* we encode a bit in advance so we're sure to hit the time, the packet will
                           be held till the right time. If we don't do that, we can insert late and
                           oggz-validate moans */
                        while (ks->subtitles_count < ks->num_subtitles && sub->t0-1.0 <= avtime+this->start_time) {
#ifdef HAVE_KATE
                            if (sub->text) {
                              oggmux_add_kate_text(&info, i, sub->t0, sub->t1, sub->text, sub->len, sub->x1, sub->x2, sub->y1, sub->y2);
                            }
                            else {
                              oggmux_add_kate_image(&info, i, sub->t0, sub->t1, &sub->kr, &sub->kp, &sub->kb);
                            }
#endif
                            ks->subtitles_count++;
                            ++sub;
                        }
                    }
                }
            }

            /* flush out the file */
            oggmux_flush (&info, video_eos + audio_eos);

            av_free_packet (&pkt);
        } while (ret >= 0 && !(audio_done && video_done));

        if (info.passno != 1) {
#ifdef HAVE_KATE
          for (i=0; i<this->n_kate_streams; ++i) {
            ff2theora_kate_stream *ks = this->kate_streams+i;
            if (ks->num_subtitles > 0) {
                double t = (info.videotime<info.audiotime?info.audiotime:info.videotime)+this->start_time;
                oggmux_add_kate_end_packet(&info, i, t);
                oggmux_flush (&info, video_eos + audio_eos);
            }
          }
#endif

          if (this->included_subtitles) {
            for (i = 0; i < this->context->nb_streams; i++) {
              if (subtitles_opened[i]) {
                AVCodecContext *enc = this->context->streams[i]->codec;
                if (enc) avcodec_close(enc);
              }
            }
          }
        }

        if (this->video_index >= 0) {
            avcodec_close(venc);
        }
        if (this->audio_index >= 0) {
            if (swr_ctx)
                swr_free(&swr_ctx);
            avcodec_close(aenc);
        }

        /* Write the index out to disk. */
        if (info.passno != 1 && !info.skeleton_3 && info.with_skeleton) {
            write_seek_index (&info);
        }

        oggmux_close(&info);
        if (ppContext)
            pp_free_context(ppContext);
        if (!info.audio_only) {
            av_free(frame_p);
            frame_dealloc(output_p);
            frame_dealloc(output_tmp_p);
            frame_dealloc(output_resized_p);
            frame_dealloc(output_buffered_p);
            frame_dealloc(output_cropped_p);
            frame_dealloc(output_padded_p);
        }
        if (dst_audio_data)
            av_freep(&dst_audio_data[0]);
        av_freep(&dst_audio_data);
        if(swr_ctx) {
            swr_close(swr_ctx);
        }
    }
    else{
        fprintf(stderr, "No video or audio stream found.\n");
    }
}

void ff2theora_close(ff2theora this) {
    sws_freeContext(this->sws_colorspace_ctx);
    sws_freeContext(this->sws_scale_ctx);
    this->sws_colorspace_ctx = NULL;
    this->sws_scale_ctx = NULL;
    /* clear out state */
    if (info.passno != 1)
      free_subtitles(this);
    this->context = NULL;
    if (info.twopass != 3) {
        av_free(this);
    }
}

static void add_frame_hooker(const char *arg)
{
#ifdef HAVE_FRAMEHOOK
    int argc = 0;
    char *argv[64];
    int i;
    char *args = av_strdup(arg);

    argv[0] = strtok(args, " ");
    while (argc < 62 && (argv[++argc] = strtok(NULL, " "))) {
    }

    i = frame_hook_add(argc, argv);
    if (i != 0) {
        fprintf(stderr, "Failed to add video hook function: %s\n", arg);
        exit(1);
    }
#endif
}

AVRational get_rational(const char* arg)
{
    const char *p;
    AVRational rational;

    rational.num = -1;
    rational.den = 1;

    p = strchr(arg, ':');
    if (!p) {
      p = strchr(arg, '/');
    }
    if (p) {
        rational.num = strtol(arg, (char **)&arg, 10);
        if (arg == p)
            rational.den = strtol(arg+1, (char **)&arg, 10);
        if (rational.num <= 0)
            rational.num = -1;
        if (rational.den <= 0)
            rational.den = 1;
    } else {
        p = strchr(arg, '.');
        if (!p) {
            rational.num = strtol(arg, (char **)&arg, 10);
            rational.den = 1;
        } else {
            av_reduce(&rational.num, &rational.den,
                      strtod(arg, (char **)&arg) * 10000,
                      10000,
                      1024*1024);
        }
    }
    return(rational);
}

int crop_check(ff2theora this, char *name, const char *arg)
{
    int crop_value = atoi(arg);
    if (crop_value < 0) {
        fprintf(stderr, "Incorrect crop size `%s'.\n",name);
        exit(1);
    }
    if ((crop_value % 2) != 0) {
        fprintf(stderr, "Crop size `%s' must be a multiple of 2.\n",name);
        exit(1);
    }
    /*
    if ((crop_value) >= this->height) {
        fprintf(stderr, "Vertical crop dimensions are outside the range of the original image.\nRemember to crop first and scale second.\n");
        exit(1);
    }
    */
    return crop_value;
}

static const struct {
  const char *name;
  int method;
} resize_methods[] = {
  { "fast-bilinear", SWS_FAST_BILINEAR },
  { "bilinear", SWS_BILINEAR },
  { "bicubic", SWS_BICUBIC },
  { "x", SWS_X },
  { "point", SWS_POINT },
  { "area", SWS_AREA },
  { "bicublin", SWS_BICUBLIN },
  { "gauss", SWS_GAUSS },
  { "sinc", SWS_SINC },
  { "lanczos", SWS_LANCZOS },
  { "spline", SWS_SPLINE },
};

static int get_resize_method_by_name(const char *name)
{
  int n;
  for (n=0; n<sizeof(resize_methods)/sizeof(resize_methods[0]); ++n) {
    if (!strcmp(resize_methods[n].name, name))
      return resize_methods[n].method;
  }
  return -1;
}

static void print_resize_help(void)
{
  int n;
  printf("Known resize methods:\n");
  for (n=0; n<sizeof(resize_methods)/sizeof(resize_methods[0]); ++n) {
    printf("  %s\n",resize_methods[n].name);
  }
}

void copy_metadata(const AVFormatContext *av)
{
    static const char *allowed[] = {
        "TITLE",
        "VERSION",
        "ALBUM",
        "TRACKNUMBER",
        "ARTIST",
        "PERFORMER",
        "COPYRIGHT",
        "LICENSE",
        "ORGANIZATION",
        "DESCRIPTION",
        "GENRE",
        "DATE",
        "LOCATION",
        "CONTACT",
        "ISRC",

        "AUTHOR"
    };
    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(av->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        char uc_key[16];
        int i;
        for (i = 0; tag->key[i] != '\0' && i < LENGTH(uc_key) - 1; i++)
            uc_key[i] = toupper(tag->key[i]);
        uc_key[i] = '\0';

        for (i = 0; i < LENGTH(allowed); i++)
            if (!strcmp(uc_key, allowed[i]))
                break;
        if (i != LENGTH(allowed)) {
            if (!strcmp(uc_key, "AUTHOR"))
                strcpy(uc_key, "ARTIST");
            if (th_comment_query(&info.tc, uc_key, 0) == NULL) {
                th_comment_add_tag(&info.tc, uc_key, tag->value);
                vorbis_comment_add_tag(&info.vc, uc_key, tag->value);
            }
        }
    }
}


void print_presets_info() {
    fprintf(stdout,
        //  "v2v presets - more info at http://wiki.v2v.cc/presets"
        "v2v presets:\n"
        "  preview        Video: 320x240 if fps ~ 30, 384x288 otherwise\n"
        "                        Quality 6\n"
        "                 Audio: Max 2 channels - Quality 1\n"
        "\n"
        "  pro            Video: 720x480 if fps ~ 30, 720x576 otherwise\n"
        "                        Quality 8\n"
        "                 Audio: Max 2 channels - Quality 3\n"
        "\n"
        "  videobin       Video: 512x288 for 16:9 material, 448x336 for 4:3 material\n"
        "                        Bitrate 600kbs\n"
        "                 Audio: Max 2 channels - Quality 3\n"
        "\n"
        "  padma          Video: 640x360 for 16:9 material, 640x480 for 4:3 material\n"
        "                        Quality 6\n"
        "                 Audio: Max 2 channels - Quality 3\n"
        "\n"
        "  padma-stream   Video: 128x72 for 16:9 material, 128x96 for 4:3 material\n"
        "                 Audio: mono quality -1\n"
        "\n"
        );
}

void print_usage() {
    th_info ti;
    th_enc_ctx *td;
    int max_speed_level = -1;

    th_info_init(&ti);
    ti.pic_width = ti.frame_width = 320;
    ti.pic_height = ti.frame_height = 240;
    td = th_encode_alloc(&ti);
    th_encode_ctl(td, TH_ENCCTL_GET_SPLEVEL_MAX, &max_speed_level, sizeof(int));
    th_encode_free(td);

    fprintf(stdout,
        PACKAGE " " PACKAGE_VERSION "\n\n"
        "\t%s\n"
        "\t%s\n",
        th_version_string(),
        vorbis_version_string());

    unsigned int version = avcodec_version();
    fprintf(stdout, "\tFFmpeg\t libavcodec %02d.%d.%d\n",
                    version >> 16, version >> 8 & 0xff, version & 0xff);
    version = avformat_version();
    fprintf(stdout, "\tFFmpeg\t libavformat %02d.%d.%d\n",
                    version >> 16, version >> 8 & 0xff, version & 0xff);

    fprintf(stdout,
        "\n\n"
        "  Usage: " PACKAGE " [options] input\n"
        "\n"
        "General output options:\n"
        "  -o, --output           alternative output filename\n"
        "      --no-skeleton      disables ogg skeleton metadata output\n"
        "      --skeleton-3       outputs Skeleton Version 3, without keyframe indexes\n"
        "  -s, --starttime        start encoding at this time (in sec.)\n"
        "  -e, --endtime          end encoding at this time (in sec.)\n"
        "  -p, --preset           encode file with preset.\n"
        "                          Right now there is preview, pro and videobin. Run\n"
        "                          '"PACKAGE" -p info' for more informations\n"
        "\n"
        "Video output options:\n"
        "  -v, --videoquality     [0 to 10] encoding quality for video (default: 6)\n"
        "                                   use higher values for better quality\n"
        "  -V, --videobitrate     encoding bitrate for video (kb/s)\n"
        "      --soft-target      Use a large reservoir and treat the rate\n"
        "                         as a soft target; rate control is less\n"
        "                         strict but resulting quality is usually\n"
        "                         higher/smoother overall. Soft target also\n"
        "                         allows an optional -v setting to specify\n"
        "                         a minimum allowed quality.\n\n"
        "      --two-pass         Compress input using two-pass rate control\n"
        "                         This option requires that the input to the\n"
        "                         to the encoder is seekable and performs\n"
        "                         both passes automatically.\n\n"
        "      --first-pass <filename> Perform first-pass of a two-pass rate\n"
        "                         controlled encoding, saving pass data to\n"
        "                         <filename> for a later second pass\n\n"
        "      --second-pass <filename> Perform second-pass of a two-pass rate\n"
        "                         controlled encoding, reading first-pass\n"
        "                         data from <filename>.  The first pass\n"
        "                         data must come from a first encoding pass\n"
        "                         using identical input video to work\n"
        "                         properly.\n\n"
        "      --optimize         optimize video output filesize (slower)\n"
        "                         (same as speedlevel 0)\n"
        "      --speedlevel       encoding is faster with higher values\n"
        "                         the cost is quality and bandwidth (default 1)\n"
        "                         available values depend on the version of libtheora\n"
        "                         your version supports speedlevels 0 to %d\n"

        "  -x, --width            scale to given width (in pixels)\n"
        "  -y, --height           scale to given height (in pixels)\n"
        "      --max_size         scale output frame to be within box of \n"
        "                         given size, height optional (%%d[x%%d], i.e. 640x480)\n"
        "      --aspect           define frame aspect ratio: i.e. 4:3 or 16:9\n"
        "      --pixel-aspect     define pixel aspect ratio: i.e. 1:1 or 4:3,\n"
        "                         overwrites frame aspect ratio\n"
        "  -F, --framerate        output framerate e.g 25:2 or 16\n"
        "      --croptop, --cropbottom, --cropleft, --cropright\n"
        "                         crop input by given pixels before resizing\n"
        "  -K, --keyint           [1 to 2147483647] keyframe interval (default: 64)\n"
        "  -d --buf-delay <n>     Buffer delay (in frames). Longer delays\n"
        "                         allow smoother rate adaptation and provide\n"
        "                         better overall quality, but require more\n"
        "                         client side buffering and add latency. The\n"
        "                         default value is the keyframe interval for\n"
        "                         one-pass encoding (or somewhat larger if\n"
        "                         --soft-target is used) and infinite for\n"
        "                         two-pass encoding. (only works in bitrate mode)\n"
        "      --no-upscaling     only scale video or resample audio if input is\n"
        "                         bigger than provided parameters\n"
        "      --resize-method <method>    Use this method for rescaling the video\n"
        "                         See --resize-method help for a list of available\n"
        "                         resizing methods\n"
        "\n"
        "Video transfer options:\n"
        "  --pp                   Video Postprocessing, denoise, deblock, deinterlacer\n"
            "                          use --pp help for a list of available filters.\n"
        "  -C, --contrast         [0.1 to 10.0] contrast correction (default: 1.0)\n"
            "                          Note: lower values make the video darker.\n"
        "  -B, --brightness       [-1.0 to 1.0] brightness correction (default: 0.0)\n"
            "                          Note: lower values make the video darker.\n"
        "  -G, --gamma            [0.1 to 10.0] gamma correction (default: 1.0)\n"
        "                          Note: lower values make the video darker.\n"
        "  -Z, --saturation       [0.1 to 10.0] saturation correction (default: 1.0)\n"
        "                          Note: lower values make the video grey.\n"
        "\n"
        "Audio output options:\n"
        "  -a, --audioquality     [-2 to 10] encoding quality for audio (default: 1)\n"
        "                                    use higher values for better quality\n"
        "  -A, --audiobitrate     [32 to 500] encoding bitrate for audio (kb/s)\n"
        "  -c, --channels         set number of output channels\n"
        "  -H, --samplerate       set output samplerate (in Hz)\n"
        "      --noaudio          disable audio from input\n"
        "      --novideo          disable video from input\n"
        "\n"
        "Input options:\n"
        "      --deinterlace      force deinterlace, otherwise only material\n"
        "                          marked as interlaced will be deinterlaced\n"
        "      --no-deinterlace   force deinterlace off\n"
#ifdef HAVE_FRAMEHOOK
        "      --vhook            you can use ffmpeg's vhook system, example:\n"
        "        ffmpeg2theora --vhook '/path/watermark.so -f wm.gif' input.dv\n"
#endif
        "  -f, --format           specify input format\n"
        "      --inputfps fps     override input fps\n"
        "      --audiostream id   by default the first audio stream is selected,\n"
        "                          use this to select another audio stream\n"
        "      --videostream id   by default the first video stream is selected,\n"
        "                          use this to select another video stream\n"
        "      --nosync           do not use A/V sync from input container.\n"
        "                         try this if you have issues with A/V sync\n"
#ifdef HAVE_KATE
        "Subtitles options:\n"
        "      --subtitles file                 use subtitles from the given file (SubRip (.srt) format)\n"
        "      --subtitles-encoding encoding    set encoding of the subtitles file\n"
#ifdef HAVE_ICONV
        "             supported are all encodings supported by iconv (see iconv help for list)\n"
#else
        "             supported are " SUPPORTED_ENCODINGS "\n"
#endif
        "      --subtitles-language language    set subtitles language (de, en_GB, etc)\n"
        "      --subtitles-category category    set subtitles category (default \"subtitles\")\n"
        "      --subtitles-ignore-non-utf8      ignores any non UTF-8 sequence in UTF-8 text\n"
        "      --nosubtitles                    disables subtitles from input\n"
        "                                       (equivalent to --subtitles=none)\n"
        "      --subtitle-types=[all,text,spu,none]   select what subtitle types to include from the\n"
        "                                             input video (default text)\n"
        "\n"
#endif
        "Metadata options:\n"
        "      --artist           Name of artist (director)\n"
        "      --title            Title\n"
        "      --date             Date\n"
        "      --location         Location\n"
        "      --organization     Name of organization (studio)\n"
        "      --copyright        Copyright\n"
        "      --license          License\n"
        "      --contact          Contact link\n"
        "      --nometadata       disables metadata from input\n"
        "      --no-oshash        do not include oshash of source file(SOURCE_OSHASH)\n"
        "\n"
        "Keyframe indexing options:\n"
        "      --index-interval <n>         set minimum distance between indexed keyframes\n"
        "                                   to <n> ms (default: 2000)\n"
        "      --theora-index-reserve <n>   reserve <n> bytes for theora keyframe index\n"
        "      --vorbis-index-reserve <n>   reserve <n> bytes for vorbis keyframe index\n"
        "      --kate-index-reserve <n>     reserve <n> bytes for kate keyframe index\n"
        "\n"
        "Other options:\n"
#ifndef _WIN32
        "      --nice n           set niceness to n\n"
#endif
        "  -P, --pid fname        write the process' id to a file\n"
        "  -h, --help             this message\n"
        "      --info             output json info about input file, use -o to save json to file\n"
        "      --frontend         print status information in json, one json dict per line\n"
        "\n"
        "\n"
        "Examples:\n"
        "  ffmpeg2theora videoclip.avi (will write output to videoclip.ogv)\n"
        "\n"
        "  ffmpeg2theora videoclip.avi --subtitles subtitles.srt (same, with subtitles)\n"
        "\n"
        "  cat something.dv | ffmpeg2theora -f dv -o output.ogv -\n"
        "\n"
        "  Encode a series of images:\n"
        "    ffmpeg2theora frame%%06d.png -o output.ogv\n"
        "\n"
        "  Live streaming from V4L Device:\n"
        "    ffmpeg2theora --no-skeleton /dev/video0 -f video4linux \\\n"
        "                  --inputfps 15 -x 160 -y 128 -o - \\\n"
        "                  | oggfwd icast2server 8000 password /theora.ogv\n"
        "\n"
        "     (you might have to use video4linux2 depending on your hardware)\n"
        "\n"
        "  Live encoding from a DV camcorder (needs a fast machine):\n"
        "    dvgrab - | ffmpeg2theora -f dv -x 352 -y 288 -o output.ogv -\n"
        "\n"
        "  Live encoding and streaming to icecast server:\n"
        "   dvgrab --format raw - \\\n"
        "    | ffmpeg2theora --no-skeleton -f dv -x 160 -y 128 -o /dev/stdout - \\\n"
        "    | oggfwd icast2server 8000 password /theora.ogv\n"
        "\n"
        ,max_speed_level);
    exit(0);
}

int main(int argc, char **argv) {
    int  n;
    int  outputfile_set=0;
    char outputfile_name[1024];
    char inputfile_name[1024];
    char *str_ptr;
    int output_json = 0;
    int output_filename_needs_building=0;

    static int flag = -1;
    static int metadata_flag = -1;

    AVInputFormat *input_fmt = NULL;
    AVDictionary *format_opts = NULL;

    int c,long_option_index;
    const char *optstring = "P:o:k:f:F:x:y:v:V:a:A:K:d:H:c:G:Z:C:B:p:N:s:e:D:h::";
    struct option options [] = {
        {"pid",required_argument,NULL, 'P'},
        {"output",required_argument,NULL,'o'},
        {"skeleton",no_argument,NULL,'k'},
        {"no-skeleton",no_argument,&flag,NOSKELETON},
        {"skeleton-3",no_argument,&flag,SKELETON_3},
        {"index-interval",required_argument,&flag,INDEX_INTERVAL},
        {"theora-index-reserve",required_argument,&flag,THEORA_INDEX_RESERVE},
        {"vorbis-index-reserve",required_argument,&flag,VORBIS_INDEX_RESERVE},
        {"kate-index-reserve",required_argument,&flag,KATE_INDEX_RESERVE},
        {"format",required_argument,NULL,'f'},
        {"width",required_argument,NULL,'x'},
        {"height",required_argument,NULL,'y'},
        {"max_size",required_argument,&flag,MAXSIZE_FLAG},
        {"videoquality",required_argument,NULL,'v'},
        {"videobitrate",required_argument,NULL,'V'},
        {"audioquality",required_argument,NULL,'a'},
        {"audiobitrate",required_argument,NULL,'A'},
        {"soft-target",0,&flag,SOFTTARGET_FLAG},
        {"two-pass",0,&flag,TWOPASS_FLAG},
        {"first-pass",required_argument,&flag,FIRSTPASS_FLAG},
        {"second-pass",required_argument,&flag,SECONDPASS_FLAG},
        {"keyint",required_argument,NULL,'K'},
        {"buf-delay",required_argument,NULL,'d'},
        {"deinterlace",0,&flag,DEINTERLACE_FLAG},
        {"no-deinterlace",0,&flag,NODEINTERLACE_FLAG},
        {"pp",required_argument,&flag,PP_FLAG},
        {"resize-method",required_argument,&flag,RESIZE_METHOD_FLAG},
        {"samplerate",required_argument,NULL,'H'},
        {"channels",required_argument,NULL,'c'},
        {"gamma",required_argument,NULL,'G'},
        {"brightness",required_argument,NULL,'B'},
        {"contrast",required_argument,NULL,'C'},
        {"saturation",required_argument,NULL,'Z'},
        {"nosound",0,&flag,NOAUDIO_FLAG},
        {"noaudio",0,&flag,NOAUDIO_FLAG},
        {"novideo",0,&flag,NOVIDEO_FLAG},
        {"nosubtitles",0,&flag,NOSUBTITLES_FLAG},
        {"subtitle-types",required_argument,&flag,SUBTITLETYPES_FLAG},
        {"nometadata",0,&flag,NOMETADATA_FLAG},
        {"no-oshash",0,&flag,NOOSHASH_FLAG},
        {"no-upscaling",0,&flag,NOUPSCALING_FLAG},
#ifdef HAVE_FRAMEHOOK
        {"vhook",required_argument,&flag,VHOOK_FLAG},
#endif
        {"framerate",required_argument,NULL,'F'},
        {"aspect",required_argument,&flag,ASPECT_FLAG},
        {"pixel-aspect",required_argument,&flag,PIXEL_ASPECT_FLAG},
        {"preset",required_argument,NULL,'p'},
        {"nice",required_argument,NULL,'N'},
        {"croptop",required_argument,&flag,CROPTOP_FLAG},
        {"cropbottom",required_argument,&flag,CROPBOTTOM_FLAG},
        {"cropright",required_argument,&flag,CROPRIGHT_FLAG},
        {"cropleft",required_argument,&flag,CROPLEFT_FLAG},
        {"inputfps",required_argument,&flag,INPUTFPS_FLAG},
        {"audiostream",required_argument,&flag,AUDIOSTREAM_FLAG},
        {"videostream",required_argument,&flag,VIDEOSTREAM_FLAG},
        {"subtitles",required_argument,&flag,SUBTITLES_FLAG},
        {"subtitles-encoding",required_argument,&flag,SUBTITLES_ENCODING_FLAG},
        {"subtitles-ignore-non-utf8",0,&flag,SUBTITLES_IGNORE_NON_UTF8_FLAG},
        {"subtitles-language",required_argument,&flag,SUBTITLES_LANGUAGE_FLAG},
        {"subtitles-category",required_argument,&flag,SUBTITLES_CATEGORY_FLAG},
        {"starttime",required_argument,NULL,'s'},
        {"endtime",required_argument,NULL,'e'},
        {"nosync",0,&flag,NOSYNC_FLAG},
        {"optimize",0,&flag,OPTIMIZE_FLAG},
        {"speedlevel",required_argument,&flag,SPEEDLEVEL_FLAG},
        {"frontend",0,&flag,FRONTEND_FLAG},
        {"frontendfile",required_argument,&flag,FRONTENDFILE_FLAG},
        {"info",no_argument,&flag,INFO_FLAG},
        {"artist",required_argument,&metadata_flag,0},
        {"title",required_argument,&metadata_flag,1},
        {"date",required_argument,&metadata_flag,2},
        {"location",required_argument,&metadata_flag,3},
        {"organization",required_argument,&metadata_flag,4},
        {"copyright",required_argument,&metadata_flag,5},
        {"license",required_argument,&metadata_flag,6},
        {"contact",required_argument,&metadata_flag,7},
        {"source-hash",required_argument,&metadata_flag,8},

        {"help",0,NULL,'h'},
        {NULL,0,NULL,0}
    };

    char pidfile_name[255] = { '\0' };
    char _tmp_2pass[1024] = { '\0' };

    FILE *fpid = NULL;

    ff2theora convert = ff2theora_init();
    avcodec_register_all();
    avdevice_register_all();
    av_register_all();

    if (argc == 1) {
        print_usage();
    }
    // set some variables;
    init_info(&info);
    th_comment_init(&info.tc);
    vorbis_comment_init(&info.vc);

    while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF) {
        switch(c)
        {
            case 0:
                if (flag) {
                    switch (flag)
                    {
                        case DEINTERLACE_FLAG:
                            convert->deinterlace = 1;
                            flag = -1;
                            break;
                        case NODEINTERLACE_FLAG:
                            convert->deinterlace = -1;
                            flag = -1;
                            break;
                        case SOFTTARGET_FLAG:
                            convert->soft_target = 1;
                            flag = -1;
                            break;
                        case TWOPASS_FLAG:
                            info.twopass = 3;
#ifdef WIN32
                            {
                              char *tmp;
                              srand (time (NULL));
                              tmp = getenv("TEMP");
                              if (!tmp) tmp = getenv("TMP");
                              if (!tmp) tmp = ".";
                              snprintf(_tmp_2pass, sizeof(_tmp_2pass), "%s\\f2t_%06d.log", tmp, rand());
                              info.twopass_file = fopen(_tmp_2pass,"wb+");
                            }
#else
                            info.twopass_file = tmpfile();
#endif
                            if(!info.twopass_file){
                                fprintf(stderr,"Unable to open temporary file for twopass data\n");
                                exit(1);
                            }
                            flag = -1;
                            break;
                        case FIRSTPASS_FLAG:
                            info.twopass = 1;
                            info.twopass_file = fopen(optarg,"wb");
                            if(!info.twopass_file){
                                fprintf(stderr,"Unable to open \'%s\' for twopass data\n", optarg);
                                exit(1);
                            }
                            flag = -1;
                            break;
                        case SECONDPASS_FLAG:
                            info.twopass = 2;
                            info.twopass_file = fopen(optarg,"rb");
                            if(!info.twopass_file){
                                fprintf(stderr,"Unable to open \'%s\' for twopass data\n", optarg);
                                exit(1);
                            }
                            flag = -1;
                            break;
                        case PP_FLAG:
                            if (!strcmp(optarg, "help")) {
                                fprintf(stdout, "%s", pp_help);
                                exit(1);
                            }
                            snprintf(convert->pp_mode,sizeof(convert->pp_mode),"%s",optarg);
                            flag = -1;
                            break;
                        case RESIZE_METHOD_FLAG:
                            if (!strcmp(optarg, "help")) {
                                print_resize_help();
                                exit(1);
                            }
                            convert->resize_method = get_resize_method_by_name(optarg);
                            flag = -1;
                            break;
                        case VHOOK_FLAG:
                            convert->vhook = 1;
                            add_frame_hooker(optarg);
                            flag = -1;
                            break;

                        case NOSYNC_FLAG:
                            convert->sync = 0;
                            flag = -1;
                            break;
                        case NOAUDIO_FLAG:
                            convert->disable_audio = 1;
                            flag = -1;
                            break;
                        case NOVIDEO_FLAG:
                            convert->disable_video = 1;
                            flag = -1;
                            break;
                        case NOSUBTITLES_FLAG:
                            convert->included_subtitles = 0;
                            flag = -1;
                            break;
                        case SUBTITLETYPES_FLAG:
                            if (!strcmp(optarg, "all")) {
                              convert->included_subtitles = INCSUB_TEXT | INCSUB_SPU;
                            }
                            else if (!strcmp(optarg, "none")) {
                              convert->included_subtitles = 0;
                            }
                            else if (!strcmp(optarg, "text")) {
                              convert->included_subtitles = INCSUB_TEXT;
                            }
                            else if (!strcmp(optarg, "spu")) {
                              convert->included_subtitles = INCSUB_SPU;
                            }
                            else {
                              fprintf(stderr,
                                 "Subtitles to include must be all, none, text, or spu.\n");
                              exit(1);
                            }
                            flag = -1;
                            break;
                        case NOMETADATA_FLAG:
                            convert->disable_metadata = 1;
                            flag = -1;
                            break;
                        case NOOSHASH_FLAG:
                            convert->disable_oshash = 1;
                            sprintf(info.oshash,"0000000000000000");
                            flag = -1;
                            break;
                        case NOUPSCALING_FLAG:
                            convert->no_upscaling = 1;
                            flag = -1;
                            break;
                        case OPTIMIZE_FLAG:
                            info.speed_level = 0;
                            flag = -1;
                            break;
                        case SPEEDLEVEL_FLAG:
                          info.speed_level = atoi(optarg);
                            flag = -1;
                            break;
                        case FRONTEND_FLAG:
                            info.frontend = stdout;
                            flag = -1;
                            break;
                        case FRONTENDFILE_FLAG:
                            info.frontend = fopen(optarg, "w");
                            flag = -1;
                            break;
                        /* crop */
                        case CROPTOP_FLAG:
                            convert->frame_topBand = crop_check(convert,"top",optarg);
                            flag = -1;
                            break;
                        case CROPBOTTOM_FLAG:
                            convert->frame_bottomBand = crop_check(convert,"bottom",optarg);
                            flag = -1;
                            break;
                        case CROPRIGHT_FLAG:
                            convert->frame_rightBand = crop_check(convert,"right",optarg);
                            flag = -1;
                            break;
                        case CROPLEFT_FLAG:
                            convert->frame_leftBand = crop_check(convert,"left",optarg);
                            flag = -1;
                            break;
                        case ASPECT_FLAG:
                            convert->frame_aspect = get_rational(optarg);
                            if(convert->frame_aspect.num == -1) {
                                fprintf(stderr,
                                   "Incorrect aspect ratio specification.\n");
                                exit(1);
                            }
                            flag = -1;
                            break;
                        case PIXEL_ASPECT_FLAG:
                            convert->pixel_aspect = get_rational(optarg);
                            if(convert->pixel_aspect.num == -1) {
                                fprintf(stderr,
                                   "Incorrect pixel aspect ratio specification.\n");
                                exit(1);
                            }
                            flag = -1;
                            break;
                        case MAXSIZE_FLAG:
                            if(sscanf(optarg, "%dx%d", &convert->max_x, &convert->max_y) != 2) {
                                convert->max_y = convert->max_x = atoi(optarg);
                            }
                            flag = -1;
                            break;
                        case INPUTFPS_FLAG:
                            convert->force_input_fps = get_rational(optarg);
                            flag = -1;
                            break;
                        case AUDIOSTREAM_FLAG:
                            convert->audiostream = atoi(optarg);
                            flag = -1;
                            break;
                        case VIDEOSTREAM_FLAG:
                            convert->videostream = atoi(optarg);
                            flag = -1;
                            break;
                        case NOSKELETON:
                            info.with_skeleton=0;
                            break;
                        case SKELETON_3:
                            info.skeleton_3 = 1;
                            break;
                        case INDEX_INTERVAL:
                            info.index_interval = atoi(optarg);
                            flag = -1;
                            break;
                        case THEORA_INDEX_RESERVE:
                            info.theora_index_reserve = atoi(optarg);
                            flag = -1;
                            break;
                        case VORBIS_INDEX_RESERVE:
                            info.vorbis_index_reserve = atoi(optarg);
                            flag = -1;
                            break;
                        case KATE_INDEX_RESERVE:
                            info.kate_index_reserve = atoi(optarg);
                            flag = -1;
                            break;
                        case INFO_FLAG:
                            output_json = 1;
                            break;
#ifdef HAVE_KATE
                        case SUBTITLES_FLAG:
                            set_subtitles_file(convert,optarg);
                            flag = -1;
                            info.with_kate=1;
                            break;
                        case SUBTITLES_ENCODING_FLAG:
                            if (is_valid_encoding(optarg)) {
                              set_subtitles_encoding(convert,optarg);
                            }
                            else {
                              report_unknown_subtitle_encoding(optarg, info.frontend);
                            }
                            flag = -1;
                            break;
                        case SUBTITLES_IGNORE_NON_UTF8_FLAG:
                            convert->ignore_non_utf8 = 1;
                            flag = -1;
                            break;
                        case SUBTITLES_LANGUAGE_FLAG:
                            if (strlen(optarg)>15) {
                              fprintf(stderr, "WARNING - language is limited to 15 characters, and will be truncated\n");
                            }
                            set_subtitles_language(convert,optarg);
                            flag = -1;
                            break;
                        case SUBTITLES_CATEGORY_FLAG:
                            if (strlen(optarg)>15) {
                              fprintf(stderr, "WARNING - category is limited to 15 characters, and will be truncated\n");
                            }
                            set_subtitles_category(convert,optarg);
                            flag = -1;
                            break;
#else
                        case SUBTITLES_FLAG:
                        case SUBTITLES_ENCODING_FLAG:
                        case SUBTITLES_IGNORE_NON_UTF8_FLAG:
                        case SUBTITLES_LANGUAGE_FLAG:
                        case SUBTITLES_CATEGORY_FLAG:
                            fprintf(stderr, "WARNING - Kate support not compiled in, subtitles will not be output\n"
                                            "        - install libkate and rebuild ffmpeg2theora for subtitle support\n");
                            break;
#endif
                    }
                }

                /* metadata */
                if (metadata_flag >= 0) {
                    static char *metadata_keys[] = {
                        "ARTIST",
                        "TITLE",
                        "DATE",
                        "LOCATION",
                        "ORGANIZATION",
                        "COPYRIGHT",
                        "LICENSE",
                        "CONTACT",
                        "SOURCE HASH"
                    };
                    th_comment_add_tag(&info.tc, metadata_keys[metadata_flag], optarg);
                    vorbis_comment_add_tag(&info.vc, metadata_keys[metadata_flag], optarg);
                    metadata_flag = -1;
                }
                break;
            case 'e':
                convert->end_time = atof(optarg);
                break;
            case 's':
                convert->start_time = atof(optarg);
                break;
            case 'o':
                snprintf(outputfile_name,sizeof(outputfile_name),"%s",optarg);
                outputfile_set=1;
                break;
            case 'k':
                info.with_skeleton=1;
                break;
            case 'P':
                snprintf(pidfile_name, sizeof(pidfile_name), "%s", optarg);
                pidfile_name[sizeof(pidfile_name)-1] = '\0';
                break;
            case 'f':
                input_fmt=av_find_input_format(optarg);
                break;
            case 'x':
                convert->picture_width=atoi(optarg);
                break;
            case 'y':
                convert->picture_height=atoi(optarg);
                break;
            case 'v':
                convert->video_quality = rint(atof(optarg)*6.3);
                if (convert->video_quality <0 || convert->video_quality >63) {
                        fprintf(stderr, "Only values from 0 to 10 are valid for video quality.\n");
                        exit(1);
                }
                break;
            case 'V':
                convert->video_bitrate=rint(atof(optarg)*1000);
                if (convert->video_bitrate < 1) {
                    fprintf(stderr, "Only positive values are allowed for video bitrate (in kb/s).\n");
                    exit(1);
                }
                break;
            case 'a':
                convert->audio_quality=atof(optarg);
                if (convert->audio_quality<-2 || convert->audio_quality>10) {
                    fprintf(stderr, "Only values from -2 to 10 are valid for audio quality.\n");
                    exit(1);
                }
                convert->audio_bitrate=0;
                break;
            case 'A':
                convert->audio_bitrate=atof(optarg)*1000;
                if (convert->audio_bitrate<0) {
                    fprintf(stderr, "Only values >0 are valid for audio bitrate.\n");
                    exit(1);
                }
                convert->audio_quality = -990;
                break;
            case 'G':
                convert->video_gamma = atof(optarg);
                break;
            case 'C':
                convert->video_contr = atof(optarg);
                break;
            case 'Z':
                convert->video_satur = atof(optarg);
                break;
            case 'B':
                convert->video_bright = atof(optarg);
                break;
            case 'K':
                convert->keyint = atoi(optarg);
                if (convert->keyint < 1 || convert->keyint > 2147483647) {
                    fprintf(stderr, "Only values from 1 to 2147483647 are valid for keyframe interval.\n");
                    exit(1);
                }
                break;
            case 'd':
                convert->buf_delay = atoi(optarg);
                break;
            case 'H':
                convert->sample_rate=atoi(optarg);
                break;
            case 'F':
                convert->framerate_new = get_rational(optarg);
                break;
            case 'c':
                convert->channels=atoi(optarg);
                if (convert->channels <= 0) {
                    fprintf(stderr, "You can not have less than one audio channel.\n");
                    exit(1);
                }
                break;
            case 'p':
                //v2v presets
                if (!strcmp(optarg, "info")) {
                    print_presets_info();
                    exit(1);
                }
                else if (!strcmp(optarg, "pro")) {
                    //need a way to set resize here. and not later
                    convert->preset=V2V_PRESET_PRO;
                    convert->video_quality = rint(8*6.3);
                    convert->audio_quality = 3.00;
                    info.speed_level = 0;
                }
                else if (!strcmp(optarg,"preview")) {
                    //need a way to set resize here. and not later
                    convert->preset=V2V_PRESET_PREVIEW;
                    convert->video_quality = rint(6*6.3);
                    convert->audio_quality = 1.00;
                    info.speed_level = 0;
                }
                else if (!strcmp(optarg,"videobin")) {
                    convert->preset=V2V_PRESET_VIDEOBIN;
                    convert->video_bitrate=rint(600*1000);
                    convert->soft_target = 1;
                    convert->video_quality = 3;
                    convert->audio_quality = 3.00;
                    info.speed_level = 0;
                }
                else if (!strcmp(optarg,"padma")) {
                    convert->preset=V2V_PRESET_PADMA;
                    convert->video_quality = rint(6*6.3);
                    convert->audio_quality = 3.00;
                    convert->channels = 2;
                    info.speed_level = 0;
                }
                else if (!strcmp(optarg,"padma-stream")) {
                    convert->preset=V2V_PRESET_PADMASTREAM;
                    convert->video_bitrate=rint(180*1000);
                    convert->soft_target = 1;
                    convert->video_quality = 0;
                    convert->audio_quality = -1.00;
                    convert->sample_rate=44100;
                    convert->channels = 1;
                    convert->keyint = 16;
                    info.speed_level = 0;
                }
                else{
                    fprintf(stderr, "\nUnknown preset.\n\n");
                    print_presets_info();
                    exit(1);
                }
                break;
            case 'N':
                n = atoi(optarg);
                if (n) {
#ifndef _WIN32
                    if (nice(n)<0) {
                        fprintf(stderr, "Error setting `%d' for niceness.", n);
                    }
#endif
                }
                break;
            case 'h':
                print_usage();
                exit(1);
        }
    }

    if (info.skeleton_3 && !info.with_skeleton) {
        fprintf(stderr, "ERROR: Cannot use --no-skeleton and --seek-index options together!\n");
        exit(1);
    }

    if (output_json && !outputfile_set) {
        snprintf(outputfile_name, sizeof(outputfile_name), "-");
        outputfile_set = 1;
    }
    if(optind<argc) {
        /* assume that anything following the options must be a filename */
        snprintf(inputfile_name,sizeof(inputfile_name),"%s",argv[optind]);
        if (!strcmp(inputfile_name,"-")) {
            snprintf(inputfile_name,sizeof(inputfile_name),"pipe:");
        }
        if (outputfile_set!=1) {
            /* we'll create an output filename based on the input name, but not now, only
               when we know what types of streams we'll ouput, as the extension we'll add
               depends on these */
            output_filename_needs_building = 1;
            outputfile_set=1;
        }
        optind++;
    } else {
        fprintf(stderr, "ERROR: no input specified\n");
        exit(1);
    }
    if(optind<argc) {
        fprintf(stderr, "WARNING: Only one input file supported, others will be ignored\n");
    }

    //FIXME: is using_stdin still neded? is it needed as global variable?
    using_stdin |= !strcmp(inputfile_name, "pipe:" ) ||
                   !strcmp( inputfile_name, "/dev/stdin" );

    if (outputfile_set != 1) {
        fprintf(stderr, "You have to specify an output file with -o output.ogv.\n");
        exit(1);
    }

    if (convert->end_time>0 && convert->end_time <= convert->start_time) {
        fprintf(stderr, "End time has to be bigger than start time.\n");
        exit(1);
    }

    if(convert->keyint <= 0) {
        /*Use a default keyframe frequency of 64 for 1-pass (streaming) mode, and
           256 for two-pass mode.*/
        convert->keyint = info.twopass?256:64;
    }

    if (convert->soft_target) {
        if (convert->video_bitrate <= 0) {
          fprintf(stderr,"Soft rate target (--soft-target) requested without a bitrate (-V).\n");
          exit(1);
        }
        if (convert->video_quality == -1)
            convert->video_quality = 0;
    } else {
        if (convert->video_quality == -1) {
            if (convert->video_bitrate > 0)
                convert->video_quality = 0;
            else
                convert->video_quality = rint(6*6.3); // default quality 5
        }
    }
    if (convert->buf_delay>0 && convert->video_bitrate == 0) {
        fprintf(stderr, "Buffer delay can only be used with target bitrate (-V).\n");
        exit(1);
    }

    if (*pidfile_name) {
        fpid = fopen(pidfile_name, "w");
        if (fpid != NULL) {
            fprintf(fpid, "%i", getpid());
            fclose(fpid);
        }
    }

    for(info.passno=(info.twopass==3?1:info.twopass);info.passno<=(info.twopass==3?2:info.twopass);info.passno++){
    //detect image sequences and set framerate if provided
    if (input_fmt != NULL && strcmp(input_fmt->name, "video4linux") >= 0) {
        char buf[100];
        av_dict_set(&format_opts, "channel", "0", 0);
        if (convert->picture_width || convert->picture_height) {
            snprintf(buf, sizeof(buf), "%dx%d",
                          convert->picture_width, convert->picture_height);
            av_dict_set(&format_opts,"video_size", buf, 0); 
        }
        if (convert->force_input_fps.num > 0) {
            snprintf(buf, sizeof(buf), "%d/%d", 
                          convert->force_input_fps.den, convert->force_input_fps.num);
            av_dict_set(&format_opts, "framerate", buf, 0);
        } else if (convert->framerate_new.num > 0) {
            snprintf(buf, sizeof(buf), "%d/%d", 
                          convert->framerate_new.den, convert->framerate_new.num);
            av_dict_set(&format_opts, "framerate", buf, 0);
        }
    }
    if (avformat_open_input(&convert->context, inputfile_name, input_fmt, &format_opts) >= 0) {
        if (avformat_find_stream_info(convert->context, NULL) >= 0) {

                if (output_filename_needs_building) {
                    int i;
                    /* work out the stream types the output will hold */
                    int has_video = 0, has_audio = 0, has_kate = 0, has_skeleton = 0;
                    for (i = 0; i < convert->context->nb_streams; i++) {
                        AVCodecContext *enc = convert->context->streams[i]->codec;
                        switch (enc->codec_type) {
                            case AVMEDIA_TYPE_VIDEO: has_video = 1; break;
                            case AVMEDIA_TYPE_AUDIO: has_audio = 1; break;
                            case AVMEDIA_TYPE_SUBTITLE: if (is_supported_subtitle_stream(convert, i, convert->included_subtitles)) has_kate = 1; break;
                            default: break;
                        }
                    }
                    has_video &= !convert->disable_video;
                    has_audio &= !convert->disable_audio;
                    has_kate &= !!convert->included_subtitles;
                    has_kate |= convert->n_kate_streams>0; /* may be added via command line */
                    has_skeleton |= info.with_skeleton;

                    /* deduce the preferred extension to use */
                    const char *ext =
                      has_video ? ".ogv" :
                      has_audio ? has_kate || has_skeleton ? ".oga" : ".ogg" :
                      ".ogx";

                    /* reserve 4 bytes in the buffer for the `.og[va]' extension */
                    snprintf(outputfile_name, sizeof(outputfile_name) - strlen(ext), "%s",inputfile_name);
                    if ((str_ptr = strrchr(outputfile_name, '.'))) {
                        sprintf(str_ptr, "%s", ext);
                        if (!strcmp(inputfile_name, outputfile_name)) {
                            snprintf(outputfile_name, sizeof(outputfile_name), "%s%s", inputfile_name, ext);
                        }
                    }
                    else {
                        snprintf(outputfile_name, sizeof(outputfile_name), "%s%s", inputfile_name, ext);
                    }
                }

                if(!convert->disable_oshash) {
#ifdef WIN32
                    sprintf(info.oshash,"%016I64x", gen_oshash(inputfile_name));
#else
                    sprintf(info.oshash,"%016qx", gen_oshash(inputfile_name));
#endif
                }
#ifdef WIN32
                if (!strcmp(outputfile_name,"-") || !strcmp(outputfile_name,"/dev/stdout")) {
                    _setmode(_fileno(stdout), _O_BINARY);
                    info.outfile = stdout;
                }
                else {
                    if(info.twopass!=1)
                        info.outfile = fopen(outputfile_name,"wb");
                }
#else
                if (!strcmp(outputfile_name,"-")) {
                    snprintf(outputfile_name,sizeof(outputfile_name),"/dev/stdout");
                }
                if(info.twopass!=1)
                    info.outfile = fopen(outputfile_name,"wb");
#endif
                if (output_json) {
                    if (using_stdin) {
                        fprintf(stderr, "can not analize input, not seekable\n");
                        exit(0);
                    } else {
                        json_format_info(info.outfile, convert->context, inputfile_name);
                        if (info.outfile != stdout)
                            fclose(info.outfile);
                        exit(0);
                    }
                }

                if (!info.frontend) {
                    if (info.twopass!=3 || info.passno==1) {
                        av_dump_format(convert->context, 0,inputfile_name, 0);
                    }
                    if (convert->disable_audio) {
                        fprintf(stderr, "  [audio disabled].\n");
                    }
                    if (convert->disable_video) {
                        fprintf(stderr, "  [video disabled].\n");
                    }
                    if (!convert->included_subtitles) {
                        fprintf(stderr, "  [subtitles disabled].\n");
                    }
                }
                if (convert->disable_metadata) {
                    if (!info.frontend)
                        fprintf(stderr, "  [metadata disabled].\n");
                } else {
                    copy_metadata(convert->context);
                }

                if (!convert->sync && !info.frontend) {
                    fprintf(stderr, "  Ignore A/V Sync from input container.\n");
                }

                convert->pts_offset = AV_NOPTS_VALUE;

                if (info.twopass!=1 && !info.outfile) {
                    if (info.frontend)
                        fprintf(info.frontend, "{\"code\": \"badfile\", \"error\":\"Unable to open output file.\"}\n");
                    else
                        fprintf(stderr,"\nUnable to open output file `%s'.\n", outputfile_name);
                    return(1);
                }
                if (convert->context->duration != AV_NOPTS_VALUE) {
                    info.duration = (double)convert->context->duration / AV_TIME_BASE - \
                                            convert->start_time;
                    if (convert->end_time)
                        info.duration = convert->end_time - convert->start_time;
                }

                ff2theora_output(convert);
        }
        else{
            if (info.frontend)
                json_format_info(info.frontend, NULL, inputfile_name);
            else if (output_json)
                json_format_info(stdout, NULL, inputfile_name);
            else
                fprintf(stderr,"\nUnable to decode input.\n");
            return(1);
        }
        avformat_close_input(&convert->context);
    }
    else{
        if (info.frontend)
            json_format_info(info.frontend, NULL, inputfile_name);
        else if (output_json)
            json_format_info(stdout, NULL, inputfile_name);
        else
            fprintf(stderr, "\nFile `%s' does not exist or has an unknown format.\n", inputfile_name);
        return(1);
    }
    ff2theora_close(convert);
    } // 2pass loop

    if (!info.frontend)
        fprintf(stderr, "\n");

    if (*pidfile_name)
        unlink(pidfile_name);
    if (info.twopass_file)
        fclose(info.twopass_file);

    if (info.frontend) {
        fprintf(info.frontend, "{\"result\": \"ok\"}\n");
        fflush(info.frontend);
    }
    if (info.frontend && info.frontend != stdout)
        fclose(info.frontend);
#ifdef WIN32
    if (info.twopass==3)
        unlink(_tmp_2pass);
#endif
    av_dict_free(&format_opts); 
    return(0);
}
