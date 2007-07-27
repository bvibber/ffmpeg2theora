/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * ffmpeg2theora.c -- Convert ffmpeg supported a/v files to  Ogg Theora / Ogg Vorbis
 * Copyright (C) 2003-2006 <j@v2v.cc>
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

#include "avformat.h"
#include "swscale.h"

#include "theora/theora.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"

#ifdef WIN32
#include "fcntl.h"
#endif

#include "theorautils.h"

#ifdef __linux__
  #define VIDEO4LINUX_ENABLED
#endif

#define DEINTERLACE_FLAG     1
#define OPTIMIZE_FLAG        2
#define SYNC_FLAG            3
#define NOSOUND_FLAG         4
#ifdef VIDEO4LINUX_ENABLED
  #define V4L_FLAG           5
#endif
#define CROPTOP_FLAG         6
#define CROPBOTTOM_FLAG      7
#define CROPRIGHT_FLAG       8
#define CROPLEFT_FLAG        9
#define ASPECT_FLAG         10
#define INPUTFPS_FLAG       11
#define AUDIOSTREAM_FLAG    12
#define VHOOK_FLAG          13
#define FRONTEND_FLAG       14
#define SPEEDLEVEL_FLAG     15

#define V2V_PRESET_PRO 1
#define V2V_PRESET_PREVIEW 2

#define PAL_HALF_WIDTH 384
#define PAL_HALF_HEIGHT 288
#define NTSC_HALF_WIDTH 320
#define NTSC_HALF_HEIGHT 240

#define PAL_FULL_WIDTH 720
#define PAL_FULL_HEIGHT 576
#define NTSC_FULL_WIDTH 720
#define NTSC_FULL_HEIGHT 480


static int sws_flags = SWS_BICUBIC;

typedef struct ff2theora{
    AVFormatContext *context;
    int video_index;
    int audio_index;
    
    int deinterlace;
    int vhook;
    int audiostream;
    int sample_rate;
    int channels;
    int disable_audio;
    float audio_quality;
    int audio_bitrate;
    int preset;

    int picture_width;
    int picture_height;
    double fps;
    struct SwsContext *sws_colorspace_ctx; /* for image resampling/resizing */
    struct SwsContext *sws_scale_ctx; /* for image resampling/resizing */
    ReSampleContext *audio_resample_ctx;
    ogg_int32_t aspect_numerator;
    ogg_int32_t aspect_denominator;
    double    frame_aspect;

    int pix_fmt;
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

    AVRational framerate_new;

    double pts_offset; /* between given input pts and calculated output pts */
    int64_t frame_count; /* total video frames output so far */
    int64_t sample_count; /* total audio samples output so far */
}
*ff2theora;

// gamma lookup table code

// ffmpeg2theora --nosound -f dv -H 32000 -S 0 -v 8 -x 384 -y 288 -G 1.5 input.dv
static double video_gamma  = 0.0;
static double video_bright = 0.0;
static double video_contr  = 0.0;
static double video_satur  = 1.0;
static int y_lut_used = 0;
static int uv_lut_used = 0;
static unsigned char y_lut[256];
static unsigned char uv_lut[256];

static void y_lut_init(unsigned char *lut, double c, double b, double g) {
    int i;
    double v;

    if ((g < 0.01) || (g > 100.0)) g = 1.0;
    if ((c < 0.01) || (c > 100.0)) c = 1.0;
    if ((b < -1.0) || (b > 1.0))   b = 0.0;

    if (g == 1.0 && c == 1.0 && b == 0.0) return;
    y_lut_used = 1;

    printf("  Video correction: gamma=%g, contrast=%g, brightness=%g\n", g, c, b);

    g = 1.0 / g;    // larger values shall make brighter video.

    for (i = 0; i < 256; i++) {
        v = (double) i / 255.0;
        v = c * v + b * 0.1;
        if (v < 0.0) v = 0.0;
        v = pow(v, g) * 255.0;    // mplayer's vf_eq2.c multiplies with 256 here, strange...

        if (v >= 255) 
            lut[i] = 255;
        else
            lut[i] = (unsigned char)(v+0.5);
    }
}


static void uv_lut_init(unsigned char *lut, double s) {
    int i;
    double v;

    if ((s < 0.0) || (s > 100.0)) s = 1.0;

    if (s == 1.0) return;
    uv_lut_used = 1;

    printf("  Color correction: saturation=%g\n", s);

    for (i = 0; i < 256; i++) {
        v = 127.0 + (s * ((double)i - 127.0));
        if (v < 0.0) v = 0.0;

        if (v >= 255.0) 
            lut[i] = 255;
        else
            lut[i] = (unsigned char)(v+0.5);
    }
}

static void lut_init(double c, double b, double g, double s) {
  y_lut_init(y_lut, c, b, g);
  uv_lut_init(uv_lut, s);
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


oggmux_info info;

static int using_stdin = 0;


/**
 * Allocate and initialise an AVFrame. 
 */
AVFrame *frame_alloc (int pix_fmt, int width, int height) {
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
 * initialize ff2theora with default values
 * @return ff2theora struct
 */
ff2theora ff2theora_init (){
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
        this->sharpness=2;
        this->keyint=64;
        this->force_input_fps=0;
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
        
        this->pix_fmt = PIX_FMT_YUV420P;
    }
    return this;
}

void ff2theora_output(ff2theora this) {
    int i;
    AVCodecContext *aenc = NULL;
    AVCodecContext *venc = NULL;
    AVStream *astream = NULL;
    AVStream *vstream = NULL;
    AVCodec *acodec = NULL;
    AVCodec *vcodec = NULL;
    float frame_aspect;
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

        if(this->force_input_fps)
            fps=this->force_input_fps;
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
        if (this->deinterlace==1)
            fprintf(stderr,"  Deinterlace: on\n");

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

        if(this->aspect_denominator && frame_aspect){
            fprintf(stderr,"  Pixel Aspect Ratio: %.2f/1 ",(float)this->aspect_numerator/this->aspect_denominator);
            fprintf(stderr,"  Frame Aspect Ratio: %.2f/1\n",frame_aspect);
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

        if (video_gamma != 0.0 || video_bright != 0.0 || video_contr != 0.0 || video_satur != 1.0) 
            lut_init(video_contr, video_bright, video_gamma, video_satur);
    }
    if (this->framerate_new.num > 0) {
        fprintf(stderr,"  Resample Framerate: %0.2f => %0.2f\n", 
                        this->fps,(double)this->framerate_new.num / this->framerate_new.den);
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

    if (this->video_index >= 0 || this->audio_index >=0){
        AVFrame *frame=NULL;
        AVFrame *frame_tmp=NULL;
        AVFrame *output=NULL;
        AVFrame *output_tmp=NULL;
        AVFrame *output_resized=NULL;
        AVFrame *output_buffered=NULL;
        
        AVPacket pkt;
        int len;
        int len1;
        int got_picture;
        int first = 1;
        int e_o_s=0;
        int ret;
        uint8_t *ptr;
        int16_t *audio_buf= av_malloc(4*AVCODEC_MAX_AUDIO_FRAME_SIZE);
        int16_t *resampled= av_malloc(4*AVCODEC_MAX_AUDIO_FRAME_SIZE);
        int no_frames;
        
        double framerate_add;
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
            frame = frame_alloc(vstream->codec->pix_fmt,
                            vstream->codec->width,vstream->codec->height);
            frame_tmp = frame_alloc(vstream->codec->pix_fmt,
                            vstream->codec->width,vstream->codec->height);
            output_tmp =frame_alloc(this->pix_fmt, 
                            vstream->codec->width,vstream->codec->height);
            output =frame_alloc(this->pix_fmt, 
                            vstream->codec->width,vstream->codec->height);
            output_resized =frame_alloc(this->pix_fmt, 
                            this->frame_width, this->frame_height);
            output_buffered =frame_alloc(this->pix_fmt,
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
            if(this->force_input_fps) {
                info.ti.fps_numerator = 1000000 * (this->fps);    /* fps= numerator/denominator */
                info.ti.fps_denominator = 1000000;
            }
            else {
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
        oggmux_init (&info);
        /*seek to start time*/
        if(this->start_time) {
          av_seek_frame( this->context, -1, (int64_t)AV_TIME_BASE*this->start_time, 1);
        }
        /*check for end time and calculate number of frames to encode*/
        no_frames = fps*(this->end_time - this->start_time);
        if(this->end_time > 0 && no_frames <= 0){
            fprintf(stderr,"End time has to be bigger than start time.\n");
            exit(1);
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
            if (e_o_s && !info.audio_only || (ret >= 0 && pkt.stream_index == this->video_index)){
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
                                        fprintf(stderr," failed deinterlace\n");
                                        // deinterlace failed
                                         output=output_tmp;
                                }
                            }
                            else{
                                output=output_tmp;
                            }
                            // now output
                            if(this->vhook)
                                frame_hook_process((AVPicture *)output, this->pix_fmt, venc->width,venc->height);

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
                    first=0;
                    //now output_resized

                    /* pysical pages */
                    yuv.y_width = this->frame_width;
                    yuv.y_height = this->frame_height;
                    yuv.y_stride = output_resized->linesize[0];

                    yuv.uv_width = this->frame_width / 2;
                    yuv.uv_height = this->frame_height / 2;
                    yuv.uv_stride = output_resized->linesize[1];

                    yuv.y = output_resized->data[0];
                    yuv.u = output_resized->data[1];
                    yuv.v = output_resized->data[2];
                    if(got_picture || e_o_s) do {
                        if (y_lut_used) 
                            lut_apply(y_lut, yuv.y, yuv.y, yuv.y_width, yuv.y_height, yuv.y_stride);
                        if (uv_lut_used) {
                            lut_apply(uv_lut, yuv.u, yuv.u, yuv.uv_width, yuv.uv_height, yuv.uv_stride);
                            lut_apply(uv_lut, yuv.v, yuv.v, yuv.uv_width, yuv.uv_height, yuv.uv_stride);
			  }
                        oggmux_add_video(&info, &yuv ,e_o_s);
                        this->frame_count++;
                    } while(dups--);
                    if(!got_picture){
                        break;
                    }
                }
                
            }
            if(e_o_s && !info.video_only 
                     || (ret >= 0 && pkt.stream_index == this->audio_index)){
                this->pts_offset = (double) pkt.pts / AV_TIME_BASE - 
                    (double) this->sample_count / this->sample_rate;
                while(e_o_s || len > 0 ){
                    int samples=0;
                    int samples_out=0;
                    int data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
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
                            }
                            else
                                resampled=audio_buf;
                        }
                    }
                    oggmux_add_audio(&info, resampled, 
                        samples_out *(this->channels),samples_out,e_o_s);
                    this->sample_count += samples_out;
                    if(e_o_s && len <= 0){
                        break;
                    }
                }

            }
            /* flush out the file */
            oggmux_flush (&info, e_o_s);
            av_free_packet (&pkt);
        }
        while (ret >= 0);
/*
    lets takes this out for now, this way glic stops complaining.
*/

/*
        if(frame) av_free(frame);
        if(frame_tmp) av_free(frame_tmp);
        if(output) av_free(output);
        if(output_tmp) av_free(output_tmp);
        if(output_resized) av_free(output_resized);
        if(output_buffered) av_free(output_buffered);
        
        if(audio_buf){
            if(audio_buf!=resampled)
                av_free(resampled);
            av_free(audio_buf);
        }

        if (this->audio_resample_ctx)
            audio_resample_close(this->audio_resample_ctx);
*/
        oggmux_close (&info);
        }
    else{
        fprintf (stderr, "No video or audio stream found.\n");
    }
}

void ff2theora_close (ff2theora this){
    /* clear out state */
    av_free (this);
}

double aspect_check(const char *arg)
{
    int x = 0, y = 0;
    double ar = 0;
    const char *p;

    p = strchr(arg, ':');
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
    
    p = strchr(arg, ':');
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
        "  -k, --skeleton         outputs ogg skeleton metadata\n"
        "  -s, --starttime        start encoding at this time (in sec.)\n"
        "  -e, --endtime          end encoding at this time (in sec.)\n"
        "  -p, --v2v-preset       encode file with v2v preset.\n"
        "                          Right now there is preview and pro. Run\n"
        "                          '"PACKAGE" -p info' for more informations\n"
        "\n"
        "Video output options:\n"
        "  -v, --videoquality     [0 to 10] encoding quality for video (default: 5)\n"
        "  -V, --videobitrate     [1 to 16778] encoding bitrate for video (kb/s)\n"
        "      --optimize         optimize video output filesize (slower) (same as speedlevel 0)\n"
        "      --speedlevel       [0 2] encoding is faster with higher values the cost is quality and bandwith\n"
        "                               this puts the encoder in VBR mode, bitrate settings no longer work\n"
        "  -x, --width            scale to given width (in pixels)\n"
        "  -y, --height           scale to given height (in pixels)\n"
        "      --aspect           define frame aspect ratio: i.e. 4:3 or 16:9\n"
        "  -F, --framerate        output framerate e.g 25:2 or 16\n"
        "      --croptop, --cropbottom, --cropleft, --cropright\n"        
        "                         crop input by given pixels before resizing\n"        
        "  -S, --sharpness        [0 to 2] sharpness of images (default: 2).\n"
        "                          Note: lower values make the video sharper.\n"
        "  -K, --keyint           [8 to 65536] keyframe interval (default: 64)\n"
        "\n"
        "Video transfer options:\n"
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
#ifdef VIDEO4LINUX_ENABLED
        "      --v4l /dev/video0  read data from v4l device /dev/video0\n"
        "                          you have to specifiy an output file with -o\n"
#endif
        "      --inputfps fps     override input fps\n"
        "      --audiostream id   by default the last audio stream is selected,\n"
        "                          use this to select another audio stream\n"
        "      --sync             use A/V sync from input container. Since this does\n"
        "                          not work with all input format you have to manually\n"
        "                          enable it if you have issues with A/V sync\n"
        "\n"
        "Metadata options:\n"
        "      --artist           Name of artist (director)\n"
        "      --title            Title\n"
        "      --date             Date\n"
        "      --location         Location\n"
        "      --organization     Name of organization (studio)\n"
        "      --copyright        Copyright\n"
        "      --license          License\n"
        "\n"
        "Other options:\n"
#ifndef _WIN32
        "      --nice n           set niceness to n\n"
#endif
        "  -P, --pid fname        write the process' id to a file\n"
        "  -h, --help             this message\n"
        "\n"
        "Examples:\n"
        "  ffmpeg2theora videoclip.avi (will write output to videoclip.ogg)\n"
        "\n"
        "  cat something.dv | ffmpeg2theora -f dv -o output.ogg -\n"
        "\n"
#ifdef VIDEO4LINUX_ENABLED
        "  Live streaming from V4L Device:\n"
        "  ffmpeg2theora --v4l /dev/video0 --inputfps 15 -x 160 -y 128 -o - \\\n"
        "   | oggfwd iccast2server 8000 password /theora.ogg\n"
        "\n"
#endif
        "  Live encoding from a DV camcorder (needs a fast machine):\n"
        "  dvgrab - | ffmpeg2theora -f dv -x 352 -y 288 -o output.ogg -\n"
        "\n"
        "  Live encoding and streaming to icecast server:\n"
        "  dvgrab --format raw - | \\\n"
        "   ffmpeg2theora -f dv -x 160 -y 128 -o /dev/stdout - | \\\n"
        "   oggfwd iccast2server 8000 password /theora.ogg\n"
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
    AVFormatParameters *formatParams = NULL;
    
    int c,long_option_index;
    const char *optstring = "P:o:kf:F:x:y:v:V:a:A:S:K:d:H:c:G:Z:C:B:p:N:s:e:D:h::";
    struct option options [] = {
      {"pid",required_argument,NULL, 'P'},
      {"output",required_argument,NULL,'o'},
      {"skeleton",no_argument,NULL,'k'},
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
      {"samplerate",required_argument,NULL,'H'},
      {"channels",required_argument,NULL,'c'},
      {"gamma",required_argument,NULL,'G'},
      {"brightness",required_argument,NULL,'B'},
      {"contrast",required_argument,NULL,'C'},
      {"saturation",required_argument,NULL,'Z'},
      {"nosound",0,&flag,NOSOUND_FLAG},
      {"vhook",required_argument,&flag,VHOOK_FLAG},
      {"framerate",required_argument,NULL,'F'},
#ifdef VIDEO4LINUX_ENABLED
      {"v4l",required_argument,&flag,V4L_FLAG},
#endif
      {"aspect",required_argument,&flag,ASPECT_FLAG},
      {"v2v-preset",required_argument,NULL,'p'},
      {"nice",required_argument,NULL,'N'},
      {"croptop",required_argument,&flag,CROPTOP_FLAG},
      {"cropbottom",required_argument,&flag,CROPBOTTOM_FLAG},
      {"cropright",required_argument,&flag,CROPRIGHT_FLAG},
      {"cropleft",required_argument,&flag,CROPLEFT_FLAG},
      {"inputfps",required_argument,&flag,INPUTFPS_FLAG},
      {"audiostream",required_argument,&flag,AUDIOSTREAM_FLAG},
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

      {"help",0,NULL,'h'},
      {NULL,0,NULL,0}
    };
    
    char pidfile_name[255] = { '\0' };
    FILE *fpid = NULL;

    ff2theora convert = ff2theora_init ();
    av_register_all ();

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
#ifdef VIDEO4LINUX_ENABLED
                        case V4L_FLAG:
                            formatParams = malloc(sizeof(AVFormatParameters));
                            formatParams->device = optarg;
                            flag = -1;
                            break;
#endif
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
                            convert->force_input_fps = atof(optarg);
                            flag = -1;
                            break;
                        case AUDIOSTREAM_FLAG:
                            convert->audiostream = atoi(optarg);
                            flag = -1;
                            break;
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
                video_gamma = atof(optarg);
                break;
            case 'C':
                video_contr = atof(optarg);
		break;
            case 'Z':
                video_satur = atof(optarg);
                break;
            case 'B':
                video_bright = atof(optarg);
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
                if (convert->keyint < 8 || convert->keyint > 65536) {
                    fprintf (stderr, "Only values from 8 to 65536 are valid for keyframe interval.\n");
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
                }
                else if(!strcmp(optarg,"preview")){
                    //need a way to set resize here. and not later
                    convert->preset=V2V_PRESET_PREVIEW;
                    convert->video_quality = rint(5*6.3);
                    convert->audio_quality = 1.00;
                    convert->sharpness = 2;
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
            /* reserve 4 bytes in the buffer for the `.ogg' extension */
            snprintf(outputfile_name, sizeof(outputfile_name) - 4, "%s", argv[optind]);
            if((str_ptr = strrchr(outputfile_name, '.'))) {
              sprintf(str_ptr, ".ogg");
              if(!strcmp(inputfile_name, outputfile_name)){
                snprintf(outputfile_name, sizeof(outputfile_name), "%s.ogg", inputfile_name);
              }
            }
            else {
                 snprintf(outputfile_name, sizeof(outputfile_name), "%s.ogg", outputfile_name);
            }
            outputfile_set=1;
        }
        optind++;
    }

#ifdef VIDEO4LINUX_ENABLED
    if(formatParams != NULL) {
        formatParams->channel = 0;
        formatParams->width = PAL_HALF_WIDTH;
        formatParams->height = PAL_HALF_HEIGHT;
        if(convert->picture_width)
            formatParams->width = convert->picture_width;
        if(convert->picture_height)
            formatParams->height = convert->picture_height;

        formatParams->time_base.den = 25;
        formatParams->time_base.num = 1;
        if(convert->force_input_fps) {

            formatParams->time_base.den = convert->force_input_fps * 1000;
            formatParams->time_base.num = 1000;

        }
        formatParams->standard = "pal";
        input_fmt = av_find_input_format("video4linux");
        sprintf(inputfile_name,"");
    }
#endif

    //FIXME: is using_stdin still neded? is it needed as global variable?
    using_stdin |= !strcmp(inputfile_name, "pipe:" ) ||
                   !strcmp( inputfile_name, "/dev/stdin" );

    if(outputfile_set!=1){    
        fprintf(stderr,"You have to specify an output file with -o output.ogg.\n");    
        exit(1);
    }

    /* could go, but so far no player supports offset_x/y */
    if(convert->picture_width % 8 ||  convert->picture_height % 8){
        fprintf(stderr,"Output size must be a multiple of 8 for now.\n");
        exit(1);
    }
    /*
    if(convert->picture_width % 4 ||  convert->picture_height % 4){
        fprintf(stderr,"Output width and height size must be a multiple of 2.\n");
        exit(1);
    }
    */
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
                  fprintf(stderr, "\nf2t ;duration: %d;\n", convert->context->duration / AV_TIME_BASE);                
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
