/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * ffmpeg2theora.c -- Convert ffmpeg supported a/v files to  Ogg Theora / Ogg Vorbis
 * Copyright (C) 2003-2008 <j@v2v.cc>
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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <math.h>
#include <errno.h>

#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libavformat/framehook.h"
#include "libswscale/swscale.h"
#include "libpostproc/postprocess.h"

#include "theora/theora.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"

#ifdef WIN32
#include "fcntl.h"
#endif

#include "theorautils.h"
#include "subtitles.h"
#include "ffmpeg2theora.h"

enum {
  NULL_FLAG,
  DEINTERLACE_FLAG,
  OPTIMIZE_FLAG,
  SYNC_FLAG,
  NOSOUND_FLAG,
  CROPTOP_FLAG,
  CROPBOTTOM_FLAG,
  CROPRIGHT_FLAG,
  CROPLEFT_FLAG,
  ASPECT_FLAG,
  INPUTFPS_FLAG,
  AUDIOSTREAM_FLAG,
  SUBTITLES_FLAG,
  SUBTITLES_ENCODING_FLAG,
  SUBTITLES_LANGUAGE_FLAG,
  SUBTITLES_CATEGORY_FLAG,
  VHOOK_FLAG,
  FRONTEND_FLAG,
  SPEEDLEVEL_FLAG,
  PP_FLAG,
  NOSKELETON
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


static int sws_flags = SWS_BICUBIC;

oggmux_info info;

static int using_stdin = 0;


/**
 * Allocate and initialise an AVFrame.
 */
static AVFrame *frame_alloc (int pix_fmt, int width, int height) {
    AVFrame *picture;
    uint8_t *picture_buf;
    int size;

    picture = avcodec_alloc_frame ();
    if (!picture)
        return NULL;
    size = avpicture_get_size (pix_fmt, width, height);
    picture_buf = av_malloc (size);
    if (!picture_buf){
        av_free (picture);
        return NULL;
    }
    avpicture_fill ((AVPicture *) picture, picture_buf,
            pix_fmt, width, height);
    return picture;
}

/**
 * Frees an AVFrame.
 */
static void frame_dealloc (AVFrame *frame) {
    if (frame) {
        avpicture_free((AVPicture*)frame);
        av_free(frame);
    }
}

/**
 * initialize ff2theora with default values
 * @return ff2theora struct
 */
static ff2theora ff2theora_init (){
    ff2theora this = calloc (1, sizeof (*this));
    if (this != NULL){
        this->disable_audio=0;
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
        this->picture_width=0;      // set to 0 to not resize the output
        this->picture_height=0;      // set to 0 to not resize the output
        this->video_quality=rint(5*6.3); // video quality 5
        this->video_bitrate=0;
        this->sharpness=0;
        this->keyint=64;
        this->force_input_fps.num = -1;
        this->force_input_fps.den = 1;
        this->sync=0;
        this->aspect_numerator=0;
        this->aspect_denominator=0;
        this->frame_aspect=0;
        this->deinterlace=0; // auto by default, if input is flaged as interlaced it will deinterlace.
        this->vhook=0;
        this->framerate_new.num = -1;
        this->framerate_new.den = 1;

        this->frame_topBand=0;
        this->frame_bottomBand=0;
        this->frame_leftBand=0;
        this->frame_rightBand=0;

        this->n_kate_streams=0;
        this->kate_streams=NULL;

        this->pix_fmt = PIX_FMT_YUV420P;

        // ffmpeg2theora --nosound -f dv -H 32000 -S 0 -v 8 -x 384 -y 288 -G 1.5 input.dv
        this->video_gamma  = 0.0;
        this->video_bright = 0.0;
        this->video_contr  = 0.0;
        this->video_satur  = 1.0;

        this->y_lut_used = 0;
        this->uv_lut_used = 0;
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

    printf("  Video correction: gamma=%g, contrast=%g, brightness=%g\n", g, c, b);

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

    printf("  Color correction: saturation=%g\n", s);

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

static void prepare_yuv_buffer(ff2theora this, yuv_buffer *yuv, AVFrame *frame) {
    /* pysical pages */
    yuv->y_width = this->frame_width;
    yuv->y_height = this->frame_height;
    yuv->y_stride = frame->linesize[0];

    yuv->uv_width = this->frame_width / 2;
    yuv->uv_height = this->frame_height / 2;
    yuv->uv_stride = frame->linesize[1];

    yuv->y = frame->data[0];
    yuv->u = frame->data[1];
    yuv->v = frame->data[2];
    if (this->y_lut_used) {
        lut_apply(this->y_lut, yuv->y, yuv->y, yuv->y_width, yuv->y_height, yuv->y_stride);
    }
    if (this->uv_lut_used) {
        lut_apply(this->uv_lut, yuv->u, yuv->u, yuv->uv_width, yuv->uv_height, yuv->uv_stride);
        lut_apply(this->uv_lut, yuv->v, yuv->v, yuv->uv_width, yuv->uv_height, yuv->uv_stride);
    }
}

void ff2theora_output(ff2theora this) {
    int i;
    AVCodecContext *aenc = NULL;
    AVCodecContext *venc = NULL;
    AVStream *astream = NULL;
    AVStream *vstream = NULL;
    AVCodec *acodec = NULL;
    AVCodec *vcodec = NULL;
    pp_mode_t *ppMode = NULL;
    pp_context_t *ppContext = NULL;
    float frame_aspect = 0;
    double fps = 0.0;

    if(this->audiostream >= 0 && this->context->nb_streams > this->audiostream) {
        AVCodecContext *enc = this->context->streams[this->audiostream]->codec;
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            this->audio_index = this->audiostream;
            fprintf(stderr,"  Using stream #0.%d as audio input\n",this->audio_index);
        }
        else {
            fprintf(stderr,"  The selected stream is not audio, falling back to automatic selection\n");
        }
    }

    for (i = 0; i < this->context->nb_streams; i++){
        AVCodecContext *enc = this->context->streams[i]->codec;
        switch (enc->codec_type){
            case CODEC_TYPE_VIDEO:
              if (this->video_index < 0)
                    this->video_index = i;
                break;
            case CODEC_TYPE_AUDIO:
                if (this->audio_index < 0 && !this->disable_audio)
                    this->audio_index = i;
                break;
            default:
                break;
        }
    }

    if (this->video_index >= 0){
        vstream = this->context->streams[this->video_index];
        venc = this->context->streams[this->video_index]->codec;
        vcodec = avcodec_find_decoder (venc->codec_id);

        fps = (double) vstream->r_frame_rate.num / vstream->r_frame_rate.den;
        if (fps > 10000)
            fps /= 1000;

        if(this->force_input_fps.num > 0)
            fps=(double)this->force_input_fps.num / this->force_input_fps.den;
        if (vcodec == NULL || avcodec_open (venc, vcodec) < 0) {
            this->video_index = -1;
        }
        this->fps = fps;


        if(this->preset == V2V_PRESET_PREVIEW){
            if(abs(this->fps-30)<1 && (venc->width!=NTSC_HALF_WIDTH || venc->height!=NTSC_HALF_HEIGHT) ){
                this->picture_width=NTSC_HALF_WIDTH;
                this->picture_height=NTSC_HALF_HEIGHT;
            }
            else {
                this->picture_width=PAL_HALF_WIDTH;
                this->picture_height=PAL_HALF_HEIGHT;
            }
        }
        else if(this->preset == V2V_PRESET_PRO){
            if(abs(this->fps-30)<1 && (venc->width!=NTSC_FULL_WIDTH || venc->height!=NTSC_FULL_HEIGHT) ){
                this->picture_width=NTSC_FULL_WIDTH;
                this->picture_height=NTSC_FULL_HEIGHT;
            }
            else {
                this->picture_width=PAL_FULL_WIDTH;
                this->picture_height=PAL_FULL_HEIGHT;
            }
        }
         else if(this->preset == V2V_PRESET_PADMA){
             int width=venc->width-this->frame_leftBand-this->frame_rightBand;
             int height=venc->height-this->frame_topBand-this->frame_bottomBand;
             if(venc->sample_aspect_ratio.den!=0 && venc->sample_aspect_ratio.num!=0) {
               height=((float)venc->sample_aspect_ratio.den/venc->sample_aspect_ratio.num) * height;
             }
             if(this->frame_aspect == 0)
               this->frame_aspect = (float)width/height;
             if(this->frame_aspect <= 1.5) {
               if(width > 640 || height > 480) {
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
               if(width > 640 || height > 360) {
                 //16:9 640 x 360
                 this->picture_width=640;
                 this->picture_height=360;
               }
               else {
                 this->picture_width=width;
                 this->picture_height=height;
               }
             }

         }
         else if(this->preset == V2V_PRESET_PADMASTREAM){
             int width=venc->width-this->frame_leftBand-this->frame_rightBand;
             int height=venc->height-this->frame_topBand-this->frame_bottomBand;
             if(venc->sample_aspect_ratio.den!=0 && venc->sample_aspect_ratio.num!=0) {
               height=((float)venc->sample_aspect_ratio.den/venc->sample_aspect_ratio.num) * height;
             }
             if(this->frame_aspect == 0)
               this->frame_aspect = (float)width/height;
             if(this->frame_aspect <= 1.5) {
                 this->picture_width=128;
                 this->picture_height=96;
             }
             else {
                 this->picture_width=128;
                 this->picture_height=72;
             }
         }
        else if(this->preset == V2V_PRESET_VIDEOBIN){
            int width=venc->width-this->frame_leftBand-this->frame_rightBand;
            int height=venc->height-this->frame_topBand-this->frame_bottomBand;
            if(venc->sample_aspect_ratio.den!=0 && venc->sample_aspect_ratio.num!=0) {
              height=((float)venc->sample_aspect_ratio.den/venc->sample_aspect_ratio.num) * height;
            }
            if( ((float)width /height) <= 1.5) {
              if(width > 448) {
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
              if(width > 512) {
                //16:9 512 x 288
                this->picture_width=512;
                this->picture_height=288;
              }
              else {
                this->picture_width=width;
                this->picture_height=height;
              }
            }

        }
        if(this->picture_height==0 &&
            (this->frame_leftBand || this->frame_rightBand || this->frame_topBand || this->frame_bottomBand) ){
            this->picture_height=venc->height-
                    this->frame_topBand-this->frame_bottomBand;
        }
        if(this->picture_width==0 &&
            (this->frame_leftBand || this->frame_rightBand || this->frame_topBand || this->frame_bottomBand) ){
            this->picture_width=venc->width-
                    this->frame_leftBand-this->frame_rightBand;
        }
        //so frame_aspect is set on the commandline
        if(this->frame_aspect != 0){
            if(this->picture_height){
                this->aspect_numerator = 10000*this->frame_aspect*this->picture_height;
                this->aspect_denominator = 10000*this->picture_width;
            }
            else{
                this->aspect_numerator = 10000*this->frame_aspect*venc->height;
                this->aspect_denominator = 10000*venc->width;
            }
            av_reduce(&this->aspect_numerator,&this->aspect_denominator,this->aspect_numerator,this->aspect_denominator,10000);
            frame_aspect=this->frame_aspect;
        }
        if(venc->sample_aspect_ratio.num!=0 && this->frame_aspect==0){
            // just use the ratio from the input
            this->aspect_numerator=venc->sample_aspect_ratio.num;
            this->aspect_denominator=venc->sample_aspect_ratio.den;
            // or we use ratio for the output
            if(this->picture_height){
                int width=venc->width-this->frame_leftBand-this->frame_rightBand;
                int height=venc->height-this->frame_topBand-this->frame_bottomBand;
                av_reduce(&this->aspect_numerator,&this->aspect_denominator,
                venc->sample_aspect_ratio.num*width*this->picture_height,
                venc->sample_aspect_ratio.den*height*this->picture_width,10000);
                frame_aspect=(float)(this->aspect_numerator*this->picture_width)/
                                (this->aspect_denominator*this->picture_height);
            }
            else{
                frame_aspect=(float)(this->aspect_numerator*venc->width)/
                                (this->aspect_denominator*venc->height);
            }
        }
        if((float)this->aspect_numerator/this->aspect_denominator < 1.09){
          this->aspect_numerator = 1;
          this->aspect_denominator = 1;
          frame_aspect=(float)(this->aspect_numerator*this->picture_width)/
                          (this->aspect_denominator*this->picture_height);
        }
        if(this->aspect_denominator && frame_aspect){
            fprintf(stderr,"  Pixel Aspect Ratio: %.2f/1 ",(float)this->aspect_numerator/this->aspect_denominator);
            fprintf(stderr,"  Frame Aspect Ratio: %.2f/1\n",frame_aspect);
        }

        if (this->deinterlace==1)
            fprintf(stderr,"  Deinterlace: on\n");

        if (strcmp(this->pp_mode, "")) {
          ppContext = pp_get_context(venc->width, venc->height, PP_FORMAT_420);
          ppMode = pp_get_mode_by_name_and_quality(this->pp_mode, PP_QUALITY_MAX);
          fprintf(stderr,"  Postprocessing: %s\n", this->pp_mode);
        }

        if(!this->picture_width)
            this->picture_width = venc->width;
        if(!this->picture_height)
            this->picture_height = venc->height;

        /* Theora has a divisible-by-sixteen restriction for the encoded video size */
        /* scale the frame size up to the nearest /16 and calculate offsets */
        this->frame_width = ((this->picture_width + 15) >>4)<<4;
        this->frame_height = ((this->picture_height + 15) >>4)<<4;

        this->frame_x_offset = 0;
        this->frame_y_offset = 0;

        if(this->frame_width > 0 || this->frame_height > 0){
            this->sws_colorspace_ctx = sws_getContext(
                          venc->width, venc->height, venc->pix_fmt,
                          venc->width, venc->height, this->pix_fmt,
                          sws_flags, NULL, NULL, NULL
            );
            this->sws_scale_ctx = sws_getContext(
                          venc->width  - (this->frame_leftBand + this->frame_rightBand),
                          venc->height  - (this->frame_topBand + this->frame_bottomBand),
                          this->pix_fmt,
                          this->frame_width, this->frame_height, this->pix_fmt,
                          sws_flags, NULL, NULL, NULL
            );
            fprintf(stderr,"  Resize: %dx%d",venc->width,venc->height);
            if(this->frame_topBand || this->frame_bottomBand ||
            this->frame_leftBand || this->frame_rightBand){
                fprintf(stderr," => %dx%d",
                    venc->width-this->frame_leftBand-this->frame_rightBand,
                    venc->height-this->frame_topBand-this->frame_bottomBand);
            }
            if(this->picture_width != (venc->width-this->frame_leftBand - this->frame_rightBand)
                || this->picture_height != (venc->height-this->frame_topBand-this->frame_bottomBand))
                fprintf(stderr," => %dx%d",this->picture_width, this->picture_height);
            fprintf(stderr,"\n");
        }

        lut_init(this);
    }
    if (this->framerate_new.num > 0 && this->fps != (double)this->framerate_new.num / this->framerate_new.den) {
        fprintf(stderr,"  Resample Framerate: %0.2f => %0.2f\n",
                        this->fps, (double)this->framerate_new.num / this->framerate_new.den);
    }
    if (this->audio_index >= 0){
        astream = this->context->streams[this->audio_index];
        aenc = this->context->streams[this->audio_index]->codec;
        acodec = avcodec_find_decoder (aenc->codec_id);
        if (this->channels < 1) {
            if (aenc->channels > 2)
                this->channels = 2;
            else
                this->channels = aenc->channels;
        }
        if(this->sample_rate==-1) {
            this->sample_rate = aenc->sample_rate;
        }
        if (this->channels != aenc->channels && aenc->codec_id == CODEC_ID_AC3)
            aenc->channels = this->channels;

        if (acodec != NULL && avcodec_open (aenc, acodec) >= 0){
            if(this->sample_rate != aenc->sample_rate || this->channels != aenc->channels){
                this->audio_resample_ctx = audio_resample_init (this->channels,aenc->channels,this->sample_rate,aenc->sample_rate);
                if(this->sample_rate!=aenc->sample_rate)
                    fprintf(stderr,"  Resample: %dHz => %dHz\n",aenc->sample_rate,this->sample_rate);
                if(this->channels!=aenc->channels)
                    fprintf(stderr,"  Channels: %d => %d\n",aenc->channels,this->channels);
            }
            else{
                this->audio_resample_ctx=NULL;
            }
        }
        else{
            this->audio_index = -1;
        }
    }

    if (this->video_index >= 0 || this->audio_index >= 0){
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

        AVPacket pkt;
        int len;
        int len1;
        int got_picture;
        int first = 1;
        int e_o_s = 0;
        int ret;
        uint8_t *ptr;
        int16_t *audio_buf=av_malloc(4*AVCODEC_MAX_AUDIO_FRAME_SIZE);
        int16_t *resampled=av_malloc(4*AVCODEC_MAX_AUDIO_FRAME_SIZE);
        int16_t *audio_p=NULL;
        int no_frames;

        double framerate_add = 0;
        double framerate_tmpcount = 0;

        if(this->video_index >= 0)
            info.audio_only=0;
        else
            info.audio_only=1;

        if(this->audio_index>=0)
            info.video_only=0;
        else
            info.video_only=1;

        if(!info.audio_only){
            frame_p = frame = frame_alloc(vstream->codec->pix_fmt,
                            vstream->codec->width,vstream->codec->height);
            output_tmp_p = output_tmp = frame_alloc(this->pix_fmt,
                            vstream->codec->width,vstream->codec->height);
            output_p = output = frame_alloc(this->pix_fmt,
                            vstream->codec->width,vstream->codec->height);
            output_resized_p = output_resized = frame_alloc(this->pix_fmt,
                            this->frame_width, this->frame_height);
            output_buffered_p = output_buffered = frame_alloc(this->pix_fmt,
                            this->frame_width, this->frame_height);

            /* video settings here */
            /* config file? commandline options? v2v presets? */

            theora_info_init (&info.ti);

            info.ti.width = this->frame_width;
            info.ti.height = this->frame_height;
            info.ti.frame_width = this->picture_width;
            info.ti.frame_height = this->picture_height;
            info.ti.offset_x = this->frame_x_offset;
            info.ti.offset_y = this->frame_y_offset;
            if (this->framerate_new.num > 0) {
                // new framerate is interger only right now,
                // so denominator is always 1
                info.ti.fps_numerator = this->framerate_new.num;
                info.ti.fps_denominator = this->framerate_new.den;
            }
            else {
                info.ti.fps_numerator=vstream->r_frame_rate.num;
                info.ti.fps_denominator = vstream->r_frame_rate.den;
            }
            /* this is pixel aspect ratio */
            info.ti.aspect_numerator=this->aspect_numerator;
            info.ti.aspect_denominator=this->aspect_denominator;
            // FIXME: is all input material with fps==25 OC_CS_ITU_REC_470BG?
            // guess not, commandline option to select colorspace would be the best.
            if((this->fps-25)<1)
                info.ti.colorspace = OC_CS_ITU_REC_470BG;
            else if(abs(this->fps-30)<1)
                info.ti.colorspace = OC_CS_ITU_REC_470M;
            else
                info.ti.colorspace = OC_CS_UNSPECIFIED;

            info.ti.target_bitrate = this->video_bitrate;
            info.ti.quality = this->video_quality;
            info.ti.dropframes_p = 0;
            info.ti.keyframe_auto_p = 1;
            info.ti.keyframe_frequency = this->keyint;
            info.ti.keyframe_frequency_force = this->keyint;
            info.ti.keyframe_data_target_bitrate = info.ti.target_bitrate * 1.5;
            info.ti.keyframe_auto_threshold = 80;
            info.ti.keyframe_mindistance = 8;
            info.ti.noise_sensitivity = 1;
            // range 0-2, 0 sharp, 2 less sharp,less bandwidth
            info.ti.sharpness = this->sharpness;

        }
        /* audio settings here */
        info.channels = this->channels;
        info.sample_rate = this->sample_rate;
        info.vorbis_quality = this->audio_quality * 0.1;
        info.vorbis_bitrate = this->audio_bitrate;
        /* subtitles */
#ifdef HAVE_KATE
        for (i=0; i<this->n_kate_streams; ++i) {
            ff2theora_kate_stream *ks = this->kate_streams+i;
            kate_info *ki = &info.kate_streams[i].ki;
            kate_info_init(ki);
            if (ks->num_subtitles > 0) {
                kate_info_set_language(ki, ks->subtitles_language);
                kate_info_set_category(ki, ks->subtitles_category[0]?ks->subtitles_category:"subtitles");
                if(this->force_input_fps.num > 0) {
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
                        ki->gps_numerator=vstream->r_frame_rate.num;
                        ki->gps_denominator = vstream->r_frame_rate.den;
                    }
                }
                ki->granule_shift = 32;
            }
        }
#endif
        oggmux_init (&info);
        /*seek to start time*/
        if(this->start_time) {
          av_seek_frame( this->context, -1, (int64_t)AV_TIME_BASE*this->start_time, 1);
          /* discard subtitles by their end time, so we still have those that start before the start time,
             but end after it */
          for (i=0; i<this->n_kate_streams; ++i) {
              ff2theora_kate_stream *ks=this->kate_streams+i;
              while (ks->subtitles_count < ks->num_subtitles && ks->subtitles[ks->subtitles_count].t1 <= this->start_time) {
                  /* printf("skipping subtitle %u\n", ks->subtitles_count); */
                  ks->subtitles_count++;
              }
          }
        }

        if(info.audio_only && (this->end_time>0 || this->start_time>0)){
            fprintf(stderr,"Sorry, right now start/end time does not work for audio only files.\n");
            exit(1);
        }

        if (this->framerate_new.num > 0) {
            double framerate_new = (double)this->framerate_new.num / this->framerate_new.den;
            framerate_add = framerate_new/this->fps;
            //fprintf(stderr,"calculating framerate addition to %f\n",framerate_add);
            this->fps = framerate_new;
        }

        /*check for end time and calculate number of frames to encode*/
        no_frames = this->fps*(this->end_time - this->start_time);
        if(this->end_time > 0 && no_frames <= 0){
            fprintf(stderr,"End time has to be bigger than start time.\n");
            exit(1);
        }
        /* main decoding loop */
        do{
            if(no_frames > 0){
                if(this->frame_count > no_frames){
                    break;
                }
            }
            ret = av_read_frame(this->context,&pkt);
            if(ret<0){
                e_o_s=1;
            }

            ptr = pkt.data;
            len = pkt.size;
            if ((e_o_s && !info.audio_only) || (ret >= 0 && pkt.stream_index == this->video_index)){
                if(len == 0 && !first && !e_o_s){
                    fprintf (stderr, "no frame available\n");
                }
                while(e_o_s || len > 0){
                    int dups = 0;
                    yuv_buffer yuv;
                    len1 = avcodec_decode_video(vstream->codec, frame, &got_picture, ptr, len);
                    if(len1>=0) {
                        if(got_picture){
                            // this is disabled by default since it does not work
                            // for all input formats the way it should.
                            if(this->sync == 1) {
                                double delta = ((double) pkt.dts /
                                    AV_TIME_BASE - this->pts_offset) *
                                    this->fps - this->frame_count;
                                /* 0.7 is an arbitrary value */
                                /* it should be larger than half a frame to
                                 avoid excessive dropping and duplicating */
                                if (delta < -0.7) {
#ifdef DEBUG
                                    fprintf(stderr,
                                          "Frame dropped to maintain sync\n");
#endif
                                    break;
                                }
                                if (delta > 0.7) {
                                    //dups = lrintf(delta);
                                    dups = (int)delta;
#ifdef DEBUG
                                    fprintf(stderr,
                                      "%d duplicate %s added to maintain sync\n",
                                      dups, (dups == 1) ? "frame" : "frames");
#endif
                                }
                            }

                            if (this->framerate_new.num > 0) {
                                framerate_tmpcount += framerate_add;
                                if (framerate_tmpcount < (double)(this->frame_count+1)) {
                                    got_picture = 0;
                                }
                                else {
                                    dups = 0;
                                    while (framerate_tmpcount >= (double)(this->frame_count+2+dups)) {
                                        dups += 1;
                                    }
                                }
                            }

                            //For audio only files command line option"-e" will not work
                            //as we don't increment frame_count in audio section.

                            if(venc->pix_fmt != this->pix_fmt) {
                               sws_scale(this->sws_colorspace_ctx,
                                 frame->data, frame->linesize, 0, venc->height,
                                 output_tmp->data, output_tmp->linesize);

                            }
                            else{
                                output_tmp = frame;
                            }
                            if(frame->interlaced_frame || this->deinterlace){
                                if(avpicture_deinterlace((AVPicture *)output,(AVPicture *)output_tmp,this->pix_fmt,venc->width,venc->height)<0){
                                        fprintf(stderr,"Deinterlace failed.\n");
                                        exit(1);
                                }
                            }
                            else{
                                output=output_tmp;
                            }
                            // now output

                            if(ppMode)
                                pp_postprocess(output->data, output->linesize,
                                               output->data, output->linesize,
                                               venc->width, venc->height,
                                               output->qscale_table, output->qstride,
                                               ppMode, ppContext, this->pix_fmt);
                            if(this->vhook)
                                frame_hook_process((AVPicture *)output, this->pix_fmt, venc->width,venc->height, 0);

                            if (this->frame_topBand || this->frame_leftBand) {
                                if (av_picture_crop((AVPicture *)output_tmp, (AVPicture *)output,
                                    this->pix_fmt, this->frame_topBand, this->frame_leftBand) < 0) {
                                    av_log(NULL, AV_LOG_ERROR, "error cropping picture\n");
                                }
                                output = output_tmp;
                            }
                            if(this->sws_scale_ctx){
                              sws_scale(this->sws_scale_ctx,
                                output->data, output->linesize, 0, venc->height - (this->frame_topBand + this->frame_bottomBand),
                                output_resized->data, output_resized->linesize);
                            }
                            else{
                                output_resized=output;
                            }

                        }
                        ptr += len1;
                        len -= len1;
                    }
                    //now output_resized

                    if(!first) {
                      if(got_picture || e_o_s) {
                        prepare_yuv_buffer(this, &yuv, output_buffered);
                        do {
                          oggmux_add_video(&info, &yuv, e_o_s);
                          this->frame_count++;
                        } while(dups--);
                      }
                    }
                    if(got_picture) {
                      first=0;
                      av_picture_copy ((AVPicture *)output_buffered, (AVPicture *)output_resized, this->pix_fmt, this->frame_width, this->frame_height);
                    }
                    if(!got_picture){
                        break;
                    }
                }

            }
            if((e_o_s && !info.video_only)
                     || (ret >= 0 && pkt.stream_index == this->audio_index)){
                this->pts_offset = (double) pkt.pts / AV_TIME_BASE -
                    (double) this->sample_count / this->sample_rate;
                while(e_o_s || len > 0 ){
                    int samples=0;
                    int samples_out=0;
                    int data_size = 4*AVCODEC_MAX_AUDIO_FRAME_SIZE;
                    if(len > 0){
                        len1 = avcodec_decode_audio2(astream->codec, audio_buf, &data_size, ptr, len);
                        if (len1 < 0){
                            /* if error, we skip the frame */
                            break;
                        }
                        len -= len1;
                        ptr += len1;
                        if(data_size >0){
                            samples =data_size / (aenc->channels * 2);

                            samples_out = samples;
                            if(this->audio_resample_ctx){
                                samples_out = audio_resample(this->audio_resample_ctx, resampled, audio_buf, samples);
                                audio_p = resampled;
                            }
                            else
                                audio_p = audio_buf;
                        }
                    }
                    oggmux_add_audio(&info, audio_p,
                        samples_out *(this->channels),samples_out,e_o_s);
                    this->sample_count += samples_out;
                    if(e_o_s && len <= 0){
                        break;
                    }
                }

            }

            /* if we have subtitles starting before then, add it */
            if (info.with_kate) {
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
                            int eos = (ks->subtitles_count == ks->num_subtitles-1);
                            oggmux_add_kate_text(&info, i, sub->t0, sub->t1, sub->text, sub->len, eos);
                            ks->subtitles_count++;
                            ++sub;
                        }
                    }
                }
            }

            /* flush out the file */
            oggmux_flush (&info, e_o_s);
            av_free_packet (&pkt);
        }
        while (ret >= 0);

        for (i=0; i<this->n_kate_streams; ++i) {
            ff2theora_kate_stream *ks = this->kate_streams+i;
            if (ks->num_subtitles > 0 && ks->subtitles_count<ks->num_subtitles) {
                double t = (info.videotime<info.audiotime?info.audiotime:info.videotime)+this->start_time;
                oggmux_add_kate_end_packet(&info, i, t);
                oggmux_flush (&info, e_o_s);
            }
        }

        if (this->video_index >= 0) {
            avcodec_close(venc);
        }
        if (this->audio_index >= 0) {
            if (this->audio_resample_ctx)
              audio_resample_close(this->audio_resample_ctx);
            avcodec_close(aenc);
        }
        oggmux_close (&info);
        if(ppContext)
            pp_free_context(ppContext);
        if (!info.audio_only) {
            frame_dealloc(output_p);
            frame_dealloc(output_tmp_p);
            frame_dealloc(output_resized_p);
            frame_dealloc(output_buffered_p);
        }
        av_free(audio_buf);
        av_free(resampled);
    }
    else{
        fprintf (stderr, "No video or audio stream found.\n");
    }
}

void ff2theora_close (ff2theora this){
    sws_freeContext(this->sws_colorspace_ctx);
    sws_freeContext(this->sws_scale_ctx);
    /* clear out state */
    free_subtitles(this);
    av_free (this);
}

double aspect_check(const char *arg)
{
    int x = 0, y = 0;
    double ar = 0;
    const char *p;

    p = strchr(arg, ':');
    if (!p) {
      p = strchr(arg, '/');
    }
    if (p) {
        x = strtol(arg, (char **)&arg, 10);
        if (arg == p)
            y = strtol(arg+1, (char **)&arg, 10);
        if (x > 0 && y > 0)
            ar = (double)x / (double)y;
    } else
        ar = strtod(arg, (char **)&arg);

    if (!ar) {
        fprintf(stderr, "Incorrect aspect ratio specification.\n");
        exit(1);
    }
    return ar;
}

static void add_frame_hooker(const char *arg)
{
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
}

AVRational get_framerate(const char* arg)
{
    const char *p;
    AVRational framerate;

    framerate.num = -1;
    framerate.den = 1;

    p = strchr(arg, ':');
    if (!p) {
      p = strchr(arg, '/');
    }
    if (p) {
        framerate.num = strtol(arg, (char **)&arg, 10);
        if (arg == p)
            framerate.den = strtol(arg+1, (char **)&arg, 10);
        if(framerate.num <= 0)
            framerate.num = -1;
        if(framerate.den <= 0)
            framerate.den = 1;
    } else {
        framerate.num = strtol(arg, (char **)&arg,10);
        framerate.den = 1;
    }
    return(framerate);
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
    if ((crop_value) >= this->height){
        fprintf(stderr, "Vertical crop dimensions are outside the range of the original image.\nRemember to crop first and scale second.\n");
        exit(1);
    }
    */
    return crop_value;
}



void print_presets_info() {
    fprintf (stdout,
        //  "v2v presets - more info at http://wiki.v2v.cc/presets"
        "v2v presets:\n"
        "  preview        Video: 320x240 if fps ~ 30, 384x288 otherwise\n"
        "                        Quality 5 - Sharpness 2\n"
        "                 Audio: Max 2 channels - Quality 1\n"
        "\n"
        "  pro            Video: 720x480 if fps ~ 30, 720x576 otherwise\n"
        "                        Quality 7 - Sharpness 0\n"
        "                 Audio: Max 2 channels - Quality 3\n"
        "\n"
        "  videobin       Video: 512x288 for 16:9 material, 448x336 for 4:3 material\n"
        "                        Bitrate 600kbs\n"
        "                 Audio: Max 2 channels - Quality 3\n"
        "\n"
        "  padma          Video: 640x360 for 16:9 material, 640x480 for 4:3 material\n"
        "                        Quality 5 - Sharpness 0\n"
        "                 Audio: Max 2 channels - Quality 3\n"
        "\n"
        "  padma-stream   Video: 128x72 for 16:9 material, 128x96 for 4:3 material\n"
        "                 Audio: mono quality -1\n"
        "\n"
        );
}

void print_usage (){
    fprintf (stdout,
        PACKAGE " " PACKAGE_VERSION "\n"
        "\n"
        "  Usage: " PACKAGE " [options] input\n"
        "\n"
        "General output options:\n"
        "  -o, --output           alternative output filename\n"
        "      --no-skeleton      disables ogg skeleton metadata output\n"
        "  -s, --starttime        start encoding at this time (in sec.)\n"
        "  -e, --endtime          end encoding at this time (in sec.)\n"
        "  -p, --v2v-preset       encode file with v2v preset.\n"
        "                          Right now there is preview, pro and videobin. Run\n"
        "                          '"PACKAGE" -p info' for more informations\n"
        "\n"
        "Video output options:\n"
        "  -v, --videoquality     [0 to 10] encoding quality for video (default: 5)\n"
        "                                   use higher values for better quality\n"
        "  -V, --videobitrate     [1 to 16778] encoding bitrate for video (kb/s)\n"
        "      --optimize         optimize video output filesize (slower) (same as speedlevel 0)\n"
        "      --speedlevel       [0 2] encoding is faster with higher values the cost is quality and bandwidth\n"
        "  -x, --width            scale to given width (in pixels)\n"
        "  -y, --height           scale to given height (in pixels)\n"
        "      --aspect           define frame aspect ratio: i.e. 4:3 or 16:9\n"
        "  -F, --framerate        output framerate e.g 25:2 or 16\n"
        "      --croptop, --cropbottom, --cropleft, --cropright\n"
        "                         crop input by given pixels before resizing\n"
        "  -S, --sharpness        [0 to 2] sharpness of images (default: 0).\n"
        "                          Note: lower values make the video sharper.\n"
        "  -K, --keyint           [1 to 65536] keyframe interval (default: 64)\n"
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
        "      --nosound          disable the sound from input\n"
        "\n"
        "Input options:\n"
        "      --deinterlace      force deinterlace, otherwise only material\n"
        "                          marked as interlaced will be deinterlaced\n"
        "      --vhook            you can use ffmpeg's vhook system, example:\n"
        "        ffmpeg2theora --vhook '/path/watermark.so -f wm.gif' input.dv\n"
        "  -f, --format           specify input format\n"
        "      --inputfps fps     override input fps\n"
        "      --audiostream id   by default the last audio stream is selected,\n"
        "                          use this to select another audio stream\n"
        "      --sync             use A/V sync from input container. Since this does\n"
        "                          not work with all input format you have to manually\n"
        "                          enable it if you have issues with A/V sync\n"
        "\n"
#ifdef HAVE_KATE
        "Subtitles options:\n"
        "      --subtitles file                 use subtitles from the given file (SubRip (.srt) format)\n"
        "      --subtitles-encoding encoding    set encoding of the subtitles file\n"
        "             supported are " SUPPORTED_ENCODINGS "\n"
        "      --subtitles-language language    set subtitles language (de, en_GB, etc)\n"
        "      --subtitles-category category    set subtitles category (default \"subtitles\")\n"
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
        "\n"
        "Other options:\n"
#ifndef _WIN32
        "      --nice n           set niceness to n\n"
#endif
        "  -P, --pid fname        write the process' id to a file\n"
        "  -h, --help             this message\n"
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
        "    ffmpeg2theora /dev/video0 -f video4linux -fps 15 -x 160 -y 128 -o - \\\n"
        "     | oggfwd iccast2server 8000 password /theora.ogv\n"
        "\n"
        "  Live encoding from a DV camcorder (needs a fast machine):\n"
        "    dvgrab - | ffmpeg2theora -f dv -x 352 -y 288 -o output.ogv -\n"
        "\n"
        "  Live encoding and streaming to icecast server:\n"
        "    dvgrab --format raw - \\\n"
        "     | ffmpeg2theora -f dv -x 160 -y 128 -o /dev/stdout - \\\n"
        "     | oggfwd iccast2server 8000 password /theora.ogv\n"
        "\n"
        );
    exit (0);
}

int main (int argc, char **argv){
    int  n;
    int  outputfile_set=0;
    char outputfile_name[255];
    char inputfile_name[255];
    char *str_ptr;

    static int flag = -1;
    static int metadata_flag = 0;

    AVInputFormat *input_fmt = NULL;
    AVFormatParameters params, *formatParams = NULL;

    int c,long_option_index;
    const char *optstring = "P:o:k:f:F:x:y:v:V:a:A:S:K:d:H:c:G:Z:C:B:p:N:s:e:D:h::";
    struct option options [] = {
      {"pid",required_argument,NULL, 'P'},
      {"output",required_argument,NULL,'o'},
      {"skeleton",no_argument,NULL,'k'},
      {"no-skeleton",no_argument,&flag,NOSKELETON},
      {"format",required_argument,NULL,'f'},
      {"width",required_argument,NULL,'x'},
      {"height",required_argument,NULL,'y'},
      {"videoquality",required_argument,NULL,'v'},
      {"videobitrate",required_argument,NULL,'V'},
      {"audioquality",required_argument,NULL,'a'},
      {"audiobitrate",required_argument,NULL,'A'},
      {"sharpness",required_argument,NULL,'S'},
      {"keyint",required_argument,NULL,'K'},
      {"deinterlace",0,&flag,DEINTERLACE_FLAG},
      {"pp",required_argument,&flag,PP_FLAG},
      {"samplerate",required_argument,NULL,'H'},
      {"channels",required_argument,NULL,'c'},
      {"gamma",required_argument,NULL,'G'},
      {"brightness",required_argument,NULL,'B'},
      {"contrast",required_argument,NULL,'C'},
      {"saturation",required_argument,NULL,'Z'},
      {"nosound",0,&flag,NOSOUND_FLAG},
      {"vhook",required_argument,&flag,VHOOK_FLAG},
      {"framerate",required_argument,NULL,'F'},
      {"aspect",required_argument,&flag,ASPECT_FLAG},
      {"v2v-preset",required_argument,NULL,'p'},
      {"nice",required_argument,NULL,'N'},
      {"croptop",required_argument,&flag,CROPTOP_FLAG},
      {"cropbottom",required_argument,&flag,CROPBOTTOM_FLAG},
      {"cropright",required_argument,&flag,CROPRIGHT_FLAG},
      {"cropleft",required_argument,&flag,CROPLEFT_FLAG},
      {"inputfps",required_argument,&flag,INPUTFPS_FLAG},
      {"audiostream",required_argument,&flag,AUDIOSTREAM_FLAG},
      {"subtitles",required_argument,&flag,SUBTITLES_FLAG},
      {"subtitles-encoding",required_argument,&flag,SUBTITLES_ENCODING_FLAG},
      {"subtitles-language",required_argument,&flag,SUBTITLES_LANGUAGE_FLAG},
      {"subtitles-category",required_argument,&flag,SUBTITLES_CATEGORY_FLAG},
      {"starttime",required_argument,NULL,'s'},
      {"endtime",required_argument,NULL,'e'},
      {"sync",0,&flag,SYNC_FLAG},
      {"optimize",0,&flag,OPTIMIZE_FLAG},
      {"speedlevel",required_argument,&flag,SPEEDLEVEL_FLAG},
      {"frontend",0,&flag,FRONTEND_FLAG},

      {"artist",required_argument,&metadata_flag,10},
      {"title",required_argument,&metadata_flag,11},
      {"date",required_argument,&metadata_flag,12},
      {"location",required_argument,&metadata_flag,13},
      {"organization",required_argument,&metadata_flag,14},
      {"copyright",required_argument,&metadata_flag,15},
      {"license",required_argument,&metadata_flag,16},
      {"contact",required_argument,&metadata_flag,17},
      {"source-hash",required_argument,&metadata_flag,18},

      {"help",0,NULL,'h'},
      {NULL,0,NULL,0}
    };

    char pidfile_name[255] = { '\0' };
    FILE *fpid = NULL;

    ff2theora convert = ff2theora_init ();
    avcodec_register_all();
    avdevice_register_all();
    av_register_all();

    if (argc == 1){
        print_usage ();
    }
    // set some variables;
    init_info(&info);
    theora_comment_init (&info.tc);

    while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF){
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
                        case PP_FLAG:
                            if(!strcmp(optarg, "help")) {
                                fprintf(stdout, pp_help);
                                exit(1);
                            }
                            snprintf(convert->pp_mode,sizeof(convert->pp_mode),"%s",optarg);
                            flag = -1;
                            break;
                        case VHOOK_FLAG:
                            convert->vhook = 1;
                            add_frame_hooker(optarg);
                            flag = -1;
                            break;

                        case SYNC_FLAG:
                            convert->sync = 1;
                            flag = -1;
                            break;
                        case NOSOUND_FLAG:
                            convert->disable_audio = 1;
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
                            info.frontend = 1;
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
                            convert->frame_aspect = aspect_check(optarg);
                            flag = -1;
                            break;
                        case INPUTFPS_FLAG:
                            convert->force_input_fps = get_framerate(optarg);
                            flag = -1;
                            break;
                        case AUDIOSTREAM_FLAG:
                            convert->audiostream = atoi(optarg);
                            flag = -1;
                            break;
                        case NOSKELETON:
                            info.with_skeleton=0;
                            break;
#ifdef HAVE_KATE
                        case SUBTITLES_FLAG:
                            set_subtitles_file(convert,optarg);
                            flag = -1;
                            info.with_kate=1;
                            break;
                        case SUBTITLES_ENCODING_FLAG:
                            if (!strcmp(optarg,"utf-8")) set_subtitles_encoding(convert,ENC_UTF8);
                            if (!strcmp(optarg,"utf8")) set_subtitles_encoding(convert,ENC_UTF8);
                            else if (!strcmp(optarg,"iso-8859-1")) set_subtitles_encoding(convert,ENC_ISO_8859_1);
                            else if (!strcmp(optarg,"latin1")) set_subtitles_encoding(convert,ENC_ISO_8859_1);
                            else report_unknown_subtitle_encoding(optarg);
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
                        case SUBTITLES_LANGUAGE_FLAG:
                        case SUBTITLES_CATEGORY_FLAG:
                            fprintf(stderr, "WARNING - Kate support not compiled in, subtitles will not be output\n"
                                            "        - install libkate and rebuild ffmpeg2theora for subtitle support\n");
                            break;
#endif
                    }
                }

                /* metadata */
                if (metadata_flag){
                    switch(metadata_flag) {
                        case 10:
                            theora_comment_add_tag(&info.tc, "ARTIST", optarg);
                            break;
                        case 11:
                            theora_comment_add_tag(&info.tc, "TITLE", optarg);
                            break;
                        case 12:
                            theora_comment_add_tag(&info.tc, "DATE", optarg);
                            break;
                        case 13:
                            theora_comment_add_tag(&info.tc, "LOCATION", optarg);
                            break;
                        case 14:
                            theora_comment_add_tag(&info.tc, "ORGANIZATION", optarg);
                            break;
                        case 15:
                            theora_comment_add_tag(&info.tc, "COPYRIGHT", optarg);
                            break;
                        case 16:
                            theora_comment_add_tag(&info.tc, "LICENSE", optarg);
                            break;
                        case 17:
                            theora_comment_add_tag(&info.tc, "CONTACT", optarg);
                            break;
                        case 18:
                            theora_comment_add_tag(&info.tc, "SOURCE HASH", optarg);
                            break;
                    }
                    metadata_flag=0;
                }
                break;
            case 'e':
                convert->end_time = atoi(optarg);
                break;
            case 's':
                convert->start_time = atoi(optarg);
                break;
            case 'o':
                snprintf(outputfile_name,sizeof(outputfile_name),"%s",optarg);
                outputfile_set=1;
                break;
            case 'k':
                info.with_skeleton=1;
                break;
            case 'P':
                sprintf(pidfile_name,optarg);
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
                if(convert->video_quality <0 || convert->video_quality >63){
                        fprintf(stderr,"Only values from 0 to 10 are valid for video quality.\n");
                        exit(1);
                }
                convert->video_bitrate=0;
                break;
            case 'V':
                convert->video_bitrate=rint(atof(optarg)*1000);
                if (convert->video_bitrate < 1) {
                    fprintf(stderr, "Only values from 1 to 16000 are valid for video bitrate (in kb/s).\n");
                    exit(1);
                }
                convert->video_quality=0;
                break;
            case 'a':
                convert->audio_quality=atof(optarg);
                if(convert->audio_quality<-2 || convert->audio_quality>10){
                    fprintf(stderr,"Only values from -2 to 10 are valid for audio quality.\n");
                    exit(1);
                }
                convert->audio_bitrate=0;
                break;
            case 'A':
                convert->audio_bitrate=atof(optarg)*1000;
                if(convert->audio_bitrate<0){
                    fprintf(stderr,"Only values >0 are valid for audio bitrate.\n");
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
            case 'S':
                convert->sharpness = atoi(optarg);
                if (convert->sharpness < 0 || convert->sharpness > 2) {
                    fprintf (stderr, "Only values from 0 to 2 are valid for sharpness.\n");
                    exit(1);
                }
                break;
            case 'K':
                convert->keyint = atoi(optarg);
                if (convert->keyint < 1 || convert->keyint > 65536) {
                    fprintf (stderr, "Only values from 1 to 65536 are valid for keyframe interval.\n");
                    exit(1);
                }
                break;
            case 'H':
                convert->sample_rate=atoi(optarg);
                break;
            case 'F':
                convert->framerate_new = get_framerate(optarg);
                break;
            case 'c':
                convert->channels=atoi(optarg);
                if(convert->channels <= 0) {
                  fprintf (stderr, "You can not have less than one audio channel.\n");
                  exit(1);
                }
                break;
            case 'p':
                //v2v presets
                if(!strcmp(optarg, "info")){
                    print_presets_info();
                    exit(1);
                }
                else if(!strcmp(optarg, "pro")){
                    //need a way to set resize here. and not later
                    convert->preset=V2V_PRESET_PRO;
                    convert->video_quality = rint(7*6.3);
                    convert->audio_quality = 3.00;
                    convert->sharpness = 0;
                    info.speed_level = 0;
                }
                else if(!strcmp(optarg,"preview")){
                    //need a way to set resize here. and not later
                    convert->preset=V2V_PRESET_PREVIEW;
                    convert->video_quality = rint(5*6.3);
                    convert->audio_quality = 1.00;
                    convert->sharpness = 2;
                    info.speed_level = 0;
                }
                else if(!strcmp(optarg,"videobin")){
                    convert->preset=V2V_PRESET_VIDEOBIN;
                    convert->video_bitrate=rint(600*1000);
                    convert->video_quality = 0;
                    convert->audio_quality = 3.00;
                    convert->sharpness = 2;
                    info.speed_level = 0;
                }
                else if(!strcmp(optarg,"padma")){
                    convert->preset=V2V_PRESET_PADMA;
                    convert->video_quality = rint(5*6.3);
                    convert->audio_quality = 3.00;
                    convert->sharpness = 0;
                    info.speed_level = 0;
                }
                else if(!strcmp(optarg,"padma-stream")){
                    convert->preset=V2V_PRESET_PADMASTREAM;
                    convert->video_bitrate=rint(180*1000);
                    convert->video_quality = 0;
                    convert->audio_quality = -1.00;
                    convert->sample_rate=44100;
                    convert->sharpness = 2;
                    convert->keyint = 16;
                    info.speed_level = 0;
                }
                else{
                    fprintf(stderr,"\nUnknown preset.\n\n");
                    print_presets_info();
                    exit(1);
                }
                break;
            case 'N':
                n = atoi(optarg);
                if (n) {
#ifndef _WIN32
                    if (nice(n)<0) {
                        fprintf(stderr,"Error setting `%d' for niceness.", n);
                    }
#endif
                }
                break;
            case 'h':
                print_usage ();
                exit(1);
        }
    }

    while(optind<argc){
        /* assume that anything following the options must be a filename */
        snprintf(inputfile_name,sizeof(inputfile_name),"%s",argv[optind]);
        if(!strcmp(inputfile_name,"-")){
            snprintf(inputfile_name,sizeof(inputfile_name),"pipe:");
        }
        if(outputfile_set!=1){
            /* reserve 4 bytes in the buffer for the `.ogv' extension */
            snprintf(outputfile_name, sizeof(outputfile_name) - 4, "%s", argv[optind]);
            if((str_ptr = strrchr(outputfile_name, '.'))) {
              sprintf(str_ptr, ".ogv");
              if(!strcmp(inputfile_name, outputfile_name)){
                snprintf(outputfile_name, sizeof(outputfile_name), "%s.ogv", inputfile_name);
              }
            }
            else {
                 snprintf(outputfile_name, sizeof(outputfile_name), "%s.ogv", outputfile_name);
            }
            outputfile_set=1;
        }
        optind++;
    }

    //FIXME: is using_stdin still neded? is it needed as global variable?
    using_stdin |= !strcmp(inputfile_name, "pipe:" ) ||
                   !strcmp( inputfile_name, "/dev/stdin" );

    if(outputfile_set!=1){
        fprintf(stderr,"You have to specify an output file with -o output.ogv.\n");
        exit(1);
    }

    if(convert->end_time>0 && convert->end_time <= convert->start_time){
        fprintf(stderr,"End time has to be bigger than start time.\n");
        exit(1);
    }

    if (*pidfile_name)
    {
        fpid = fopen(pidfile_name, "w");
        if (fpid != NULL)
        {
            fprintf(fpid, "%i", getpid());
            fclose(fpid);
        }
    }

    oggmux_setup_kate_streams(&info, convert->n_kate_streams);

    for (n=0; n<convert->n_kate_streams; ++n) {
        ff2theora_kate_stream *ks=convert->kate_streams+n;
        if (load_subtitles(ks)>=0) {
          printf("Muxing Kate stream %d from %s as %s %s\n",
              n,ks->filename,
              ks->subtitles_language[0]?ks->subtitles_language:"<unknown language>",
              ks->subtitles_category[0]?ks->subtitles_category:"subtitles");
        }
        else {
          ks->filename = NULL;
          ks->num_subtitles = 0;
          ks->subtitles = NULL;
        }
    }
    //detect image sequences and set framerate if provided
    if(av_guess_image2_codec(inputfile_name) != CODEC_ID_NONE || \
       (input_fmt != NULL && strcmp(input_fmt->name, "video4linux") >= 0)) {
        formatParams = &params;
        memset(formatParams, 0, sizeof(*formatParams));
        if(input_fmt != NULL && strcmp(input_fmt->name, "video4linux") >= 0) {
            formatParams->channel = 0;
            formatParams->width = PAL_HALF_WIDTH;
            formatParams->height = PAL_HALF_HEIGHT;
            formatParams->time_base.den = 25;
            formatParams->time_base.num = 2;
            if(convert->picture_width)
                formatParams->width = convert->picture_width;
            if(convert->picture_height)
                formatParams->height = convert->picture_height;
        }
        if(convert->force_input_fps.num > 0) {
            formatParams->time_base.den = convert->force_input_fps.num;
            formatParams->time_base.num = convert->force_input_fps.den;
        } else if(convert->framerate_new.num > 0) {
            formatParams->time_base.den = convert->framerate_new.num;
            formatParams->time_base.num = convert->framerate_new.den;
        }
        formatParams->video_codec_id = av_guess_image2_codec(inputfile_name);
    }
    if (av_open_input_file(&convert->context, inputfile_name, input_fmt, 0, formatParams) >= 0){
        if (av_find_stream_info (convert->context) >= 0){
#ifdef WIN32
                if(!strcmp(outputfile_name,"-") || !strcmp(outputfile_name,"/dev/stdout")){
                    _setmode(_fileno(stdout), _O_BINARY);
                    info.outfile = stdout;
                }
                else {
                    info.outfile = fopen(outputfile_name,"wb");
                }
#else
                if(!strcmp(outputfile_name,"-")){
                    snprintf(outputfile_name,sizeof(outputfile_name),"/dev/stdout");
                }
                info.outfile = fopen(outputfile_name,"wb");
#endif
                if(info.frontend) {
                  fprintf(stderr, "\nf2t ;duration: %d;\n", (int)(convert->context->duration / AV_TIME_BASE));
                }
                else {
                  dump_format (convert->context, 0,inputfile_name, 0);
                }
                if(convert->disable_audio){
                    fprintf(stderr,"  [audio disabled].\n");
                }
                if(convert->sync){
                    fprintf(stderr,"  Use A/V Sync from input container.\n");
                }

                convert->pts_offset =
                    (double) convert->context->start_time / AV_TIME_BASE;
                if(!info.outfile) {
                    if(info.frontend)
                        fprintf(stderr, "\nf2t ;result: Unable to open output file.;\n");
                    else
                      fprintf (stderr,"\nUnable to open output file `%s'.\n", outputfile_name);
                    return(1);
                }
                if (convert->context->duration != AV_NOPTS_VALUE) {
                  info.duration = convert->context->duration / AV_TIME_BASE;
                }
                ff2theora_output (convert);
                convert->audio_index =convert->video_index = -1;
            }
            else{
              if(info.frontend)
                  fprintf(stderr, "\nf2t ;result: input format not suported.;\n");
              else
                  fprintf (stderr,"\nUnable to decode input.\n");
              return(1);
            }
            av_close_input_file (convert->context);
        }
        else{
            fprintf (stderr, "\nFile `%s' does not exist or has an unknown data format.\n", inputfile_name);
            return(1);
        }
    ff2theora_close (convert);
    fprintf(stderr,"\n");

    if (*pidfile_name)
        unlink(pidfile_name);

    if(info.frontend)
        fprintf(stderr, "\nf2t ;result: ok;\n");

    return(0);
}
