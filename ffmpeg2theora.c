/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * ffmpeg2theora.c -- Convert ffmpeg supported a/v files to  Ogg Theora / Ogg Vorbis
 * Copyright (C) 2003-2005 <j@v2v.cc>
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
#include "common.h"
#include "avformat.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include "theora/theora.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"

#ifdef WIN32
#include "fcntl.h"
#endif

#include "theorautils.h"

#define DEINTERLACE_FLAG     1
#define SYNC_FLAG            3
#define NOSOUND_FLAG         4
#define V4L_FLAG             5
#define CROPTOP_FLAG         6
#define CROPBOTTOM_FLAG      7
#define CROPRIGHT_FLAG       8
#define CROPLEFT_FLAG        9
#define ASPECT_FLAG         10
#define INPUTFPS_FLAG       11
#define AUDIOSTREAM_FLAG    12



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
    int preset;

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


static double rint(double x) {
    if (x < 0.0)
        return (double)(int)(x - 0.5);
    else
        return (double)(int)(x + 0.5);
}

oggmux_info info;

static int using_stdin = 0;


/**
 * Allocate and initialise an AVFrame. 
 */
AVFrame *alloc_picture (int pix_fmt, int width, int height) {
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
        this->sample_rate = 44100;  // samplerate hmhmhm
        this->channels = 2;
        this->audio_quality=0.297;// audio quality 3
        this->audio_bitrate=0;
        this->audiostream = -1;
        
        // video
        this->picture_width=0;      // set to 0 to not resize the output
        this->picture_height=0;      // set to 0 to not resize the output
        this->video_quality=31.5; // video quality 5
        this->video_bitrate=0;
        this->sharpness=2;
        this->keyint=64;
        this->force_input_fps=0;
        this->sync=0;
        this->aspect_numerator=0;
        this->aspect_denominator=0;
        this->frame_aspect=0;
        this->deinterlace=0; // auto by default, if input is flaged as interlaced it will deinterlace. 
        
        this->frame_topBand=0;
        this->frame_bottomBand=0;
        this->frame_leftBand=0;
        this->frame_rightBand=0;
        
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
        if (vcodec == NULL || avcodec_open (venc, vcodec) < 0)
            this->video_index = -1;

        this->fps = fps;
        
        if(this->preset == V2V_PRESET_PREVIEW){
            // possible sizes 384/288,320/240
            int pal_width=384;
            int pal_height=288;
            int ntsc_width=320;
            int ntsc_height=240;
            if(this->fps==25 && (venc->width!=pal_width || venc->height!=pal_height) ){
                this->picture_width=pal_width;
                this->picture_height=pal_height;
            }
            else if(abs(this->fps-30)<1 && (venc->width!=ntsc_width || venc->height!=ntsc_height) ){
                this->picture_width=ntsc_width;
                this->picture_height=ntsc_height;
            }
        }
        else if(this->preset == V2V_PRESET_PRO){
            if(this->fps==25 && (venc->width!=720 || venc->height!=576) ){
                this->picture_width=720;
                this->picture_height=576;
            }
            else if(abs(this->fps-30)<1 && (venc->width!=720 || venc->height!=480) ){
                this->picture_width=720;
                this->picture_height=480;
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
        if(this->frame_aspect!=0){
                if(this->picture_height){
                    this->aspect_numerator=10000*this->frame_aspect*this->picture_height;
                    this->aspect_denominator=10000*this->picture_width;
                }
                else{
                    this->aspect_numerator=10000*this->frame_aspect*venc->height;
                    this->aspect_denominator=10000*venc->width;
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

        
        /* Theora has a divisible-by-sixteen restriction for the encoded video size */  /* scale the frame size up to the nearest /16 and calculate offsets */

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
            int frame_padtop = this->frame_y_offset;
            int frame_padbottom = this->frame_height-this->picture_height;
            int frame_padleft = this->frame_x_offset;
            int frame_padright = this->frame_width-this->picture_width;

            this->img_resample_ctx = img_resample_full_init(
                          //this->picture_width, this->picture_height,
                          this->frame_width, this->frame_height,
                          venc->width, venc->height,
                          this->frame_topBand, this->frame_bottomBand,
                          this->frame_leftBand, this->frame_rightBand,
                          frame_padtop, frame_padbottom,
                          frame_padleft, frame_padright
              );
            fprintf(stderr,"  Resize: %dx%d",venc->width,venc->height);
            if(this->frame_topBand || this->frame_bottomBand ||
            this->frame_leftBand || this->frame_rightBand){
                fprintf(stderr," => %dx%d",
                    venc->width-this->frame_leftBand-this->frame_rightBand,
                    venc->height-this->frame_topBand-this->frame_bottomBand);
            }
            if(this->picture_width != (venc->width-this->frame_leftBand-this->frame_rightBand) 
                || this->picture_height != (venc->height-this->frame_topBand-this->frame_bottomBand))
                fprintf(stderr," => %dx%d",this->picture_width, this->picture_height);
            fprintf(stderr,"\n");
            
        }
    }

    if (this->audio_index >= 0){
        astream = this->context->streams[this->audio_index];
        aenc = this->context->streams[this->audio_index]->codec;
        acodec = avcodec_find_decoder (aenc->codec_id);
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

        if(this->video_index >= 0)
            info.audio_only=0;
        else
            info.audio_only=1;
        
        if(this->audio_index>=0)
            info.video_only=0;
        else
            info.video_only=1;
        
        if(!info.audio_only){
            frame = alloc_picture(vstream->codec->pix_fmt,
                            vstream->codec->width,vstream->codec->height);
            frame_tmp = alloc_picture(vstream->codec->pix_fmt,
                            vstream->codec->width,vstream->codec->height);
            output_tmp =alloc_picture(PIX_FMT_YUV420P, 
                            vstream->codec->width,vstream->codec->height);
            output =alloc_picture(PIX_FMT_YUV420P, 
                            vstream->codec->width,vstream->codec->height);
            output_resized =alloc_picture(PIX_FMT_YUV420P, 
                            this->frame_width, this->frame_height);
            output_buffered =alloc_picture(PIX_FMT_YUV420P,
                            this->frame_width, this->frame_height);    
        }

        if(!info.audio_only){
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
                info.ti.fps_numerator=vstream->r_frame_rate.num;
                info.ti.fps_denominator = vstream->r_frame_rate.den;
            }
            /* this is pixel aspect ratio */
            info.ti.aspect_numerator=this->aspect_numerator;
            info.ti.aspect_denominator=this->aspect_denominator;
            // FIXME: is all input material with fps==25 OC_CS_ITU_REC_470BG?
            // guess not, commandline option to select colorspace would be the best.
            if(this->fps==25)
                info.ti.colorspace = OC_CS_ITU_REC_470BG;
            else if(abs(this->fps-30)<1)
                info.ti.colorspace = OC_CS_ITU_REC_470M;
            else
                info.ti.colorspace = OC_CS_UNSPECIFIED;

            info.ti.target_bitrate = this->video_bitrate; 
            info.ti.quality = this->video_quality;
            info.ti.dropframes_p = 0;
            info.ti.quick_p = 1;
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
        info.vorbis_quality = this->audio_quality;
        info.vorbis_bitrate = this->audio_bitrate;
        oggmux_init (&info);
        /*seek to start time*/    
#if LIBAVFORMAT_BUILD <= 4616
        av_seek_frame( this->context, -1, (int64_t)AV_TIME_BASE*this->start_time);
#else
        av_seek_frame( this->context, -1, (int64_t)AV_TIME_BASE*this->start_time, 1);
#endif
        /*check for end time and caclulate number of frames to encode*/
        no_frames = fps*(this->end_time - this->start_time);
        if(this->end_time > 0 && no_frames <= 0){
            fprintf(stderr,"end time has to be bigger than start time\n");
            exit(1);
        }
        if(info.audio_only && (this->end_time>0 || this->start_time>0)){
            fprintf(stderr,"sorry, right now start/end time does not work for audio only files\n");
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
            if (e_o_s && !info.audio_only || (ret >= 0 && pkt.stream_index == this->video_index)){
                if(len == 0 && !first && !e_o_s){
                    fprintf (stderr, "no frame available\n");
                }
                while(e_o_s || len > 0){
                    int dups = 0;
                    if(len >0 &&
                        (len1 = avcodec_decode_video(vstream->codec,
                                        frame, &got_picture, ptr, len))>0) {
                                        
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
                                    fprintf(stderr,
                                          "Frame dropped to maintain sync\n");
                                    break;
                                }
                                if (delta > 0.7) {
                                    //dups = lrintf(delta);
                                    dups = (int)delta;
                                    fprintf(stderr,
                                      "%d duplicate %s added to maintain sync\n",
                                      dups, (dups == 1) ? "frame" : "frames");
                                }
                            }
                            //For audio only files command line option"-e" will not work
                            //as we don't increment frame_count in audio section.
                            if(venc->pix_fmt != PIX_FMT_YUV420P) {
                                img_convert((AVPicture *)output_tmp,PIX_FMT_YUV420P,
                                        (AVPicture *)frame,venc->pix_fmt,
                                             venc->width,venc->height);
                            }
                            else{
                                output_tmp = frame;
                            }

                            if(frame->interlaced_frame || this->deinterlace){
                                if(avpicture_deinterlace((AVPicture *)output,(AVPicture *)output_tmp,PIX_FMT_YUV420P,venc->width,venc->height)<0){                                fprintf(stderr," failed deinterlace\n");
                                        // deinterlace failed
                                         output=output_tmp;
                                }
                            }
                            else{
                                output=output_tmp;
                            }

                            // now output
                            if(this->img_resample_ctx){
                                img_resample(this->img_resample_ctx, 
                                        (AVPicture *)output_resized, 
                                        (AVPicture *)output);
                            }    
                            else{
                                output_resized=output;
                            }
                        }
                        else
                            fprintf(stderr,"did not get a pic\n");
                        ptr += len1;
                        len -= len1;
                    }    
                    first=0;
                    //now output_resized
                    /* pysical pages */
                    yuv_buffer yuv;
                    /* Theora is a one-frame-in,one-frame-out system; submit a frame
                     * for compression and pull out the packet */
                    yuv.y_width = this->frame_width;
                    yuv.y_height = this->frame_height;
                    yuv.y_stride = output_resized->linesize[0];

                    yuv.uv_width = this->frame_width / 2;
                    yuv.uv_height = this->frame_height / 2;
                    yuv.uv_stride = output_resized->linesize[1];

                    yuv.y = output_resized->data[0];
                    yuv.u = output_resized->data[1];
                    yuv.v = output_resized->data[2];

                    do {                        
                        oggmux_add_video(&info, &yuv ,e_o_s);
                        this->frame_count++;
                    } while(dups--);
                    if(e_o_s){
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
                    int data_size;
                    if(len > 0){
                        len1 = avcodec_decode_audio(astream->codec, audio_buf,&data_size, ptr, len);
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

        if (this->img_resample_ctx)
            img_resample_close(this->img_resample_ctx);
        if (this->audio_resample_ctx)
            audio_resample_close(this->audio_resample_ctx);
*/
        oggmux_close (&info);
    }
    else{
        fprintf (stderr, "No video or audio stream found\n");
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

int crop_check(ff2theora this, char *name, const char *arg)
{
    int crop_value = atoi(arg); 
    if (crop_value < 0) {
        fprintf(stderr, "Incorrect %s crop size\n",name);
        exit(1);
    }
    if ((crop_value % 2) != 0) {
        fprintf(stderr, "%s crop size must be a multiple of 2\n",name);
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
    fprintf (stderr, 
        //  "v2v presets - more info at http://wiki.v2v.cc/presets"
        "v2v presets\n"
        "preview\t\thalf size DV video. encoded with Ogg Theora/Ogg Vorbis\n"
        "\t\t\tVideo: Quality 5;\n"
        "\t\t\tAudio: Quality 1 - 44,1kHz - Stereo\n\n"
        "pro\t\tfull size DV video. encoded with Ogg Theora/Ogg Vorbis \n"
        "\t\t\tVideo: Quality 7;\n"
        "\t\t\tAudio: Quality 3 - 48kHz - Stereo\n"
        "\n");
}

void print_usage (){
    fprintf (stdout, 
        PACKAGE " " PACKAGE_VERSION "\n\n"
        " usage: " PACKAGE " [options] input\n\n"
    
        "Output options:\n"
        "\t --output,-o\t\talternative output\n"
        "\t --width, -x\t\tscale to given size\n"
        "\t --height,-y\t\tscale to given size\n"
        "\t --aspect\t\tdefine frame aspect ratio: i.e. 4:3 or 16:9\n"
        "\t --crop[top|bottom|left|right]\tcrop input before resizing\n"        
        "\t --videoquality,-v\t[0 to 10]    encoding quality for video\n"
        "\t --videobitrate,-V\t[45 to 2000] encoding bitrate for video\n"
        "\t --sharpness,-S  \t[0 to 2]     sharpness of images(default 2)\n"
        "\t                 \t   { lower values make the video sharper. }\n"
        "\t --keyint,-K      \t[8 to 65536] keyframe interval (default: 64)\n"
        "\t --audioquality,-a\t[-1 to 10]   encoding quality for audio\n"
        "\t --audiobitrate,-A\t[45 to 2000] encoding bitrate for audio\n"
        "\t --samplerate,-H\tset output samplerate in Hz\n"
        "\t --channels,-c\t\tset number of output sound channels\n"
        "\t --nosound\t\tdisable the sound from input\n"
        "\t --endtime,-e\t\tend encoding at this time (in sec)\n"
        "\t --starttime,-s\t\tstart encoding at this time (in sec)\n"
        "\t --v2v-preset,-p\tencode file with v2v preset, \n"
        "\t\t\t\t right now there is preview and pro,\n"
        "\t\t\t\t '"PACKAGE" -p info' for more informations\n"
    
        "\nInput options:\n"
        "\t --deinterlace \tforce deinterlace\n"
        "\t\t\t\t otherwise only material marked as interlaced \n"
        "\t\t\t\t will be deinterlaced\n"
        "\t --format,-f\t\tspecify input format\n"
        "\t --v4l   /dev/video0\tread data from v4l device /dev/video0\n"
        "\t            \t\tyou have to specifiy an output file with -o\n"
        "\t --inputfps [fps]\toverride input fps\n"
        "\t --audiostream streamid\tby default the last audiostream is selected,\n"
        "\t                 \tuse this to select another audio stream\n"
        "\t --sync\t\t\tUse A/V Sync from input container.\n"
        "\t       \t\tsince this does not work with all input format\n"
        "\t       \t\tyou have to manualy enable it if you have issues\n"
        "\t       \t\twith A/V sync.\n"
        
        "\nMetadata options:\n"
        "\t --artist\tName of artist (director)\n"
        "\t --title\tTitle\n"
        "\t --date\t\tDate\n"
        "\t --location\tLocation\n"
        "\t --organization\tName of organization (studio)\n"
        "\t --copyright\tCopyright\n"
        "\t --license\tLicence\n"
        "\nOther options:\n"
#ifndef _WIN32
        "\t --nice\t\t\tset niceness to n\n"
#endif
        "\t --help,-h\t\tthis message\n"


        "\n Examples:\n"
    
        "\tffmpeg2theora videoclip.avi (will write output to videoclip.ogg)\n\n"
        "\tcat something.dv | ffmpeg2theora -f dv -o output.ogg -\n\n"
        "\tLive streaming from V4L Device:\n"
        "\t ffmpeg2theora --v4l /dev/video0 --inputfps 15 -x 160 -y 128 -o - \\ \n"
        "\t\t | oggfwd iccast2server 8000 password /theora.ogg\n\n"
        "\tLive encoding from a DV camcorder (needs a fast machine):\n"
        "\t dvgrab - | ffmpeg2theora -f dv -x 352 -y 288 -o output.ogg -\n"
        "\n\tLive encoding and streaming to icecast server:\n"
        "\t dvgrab --format raw - | \\\n"
        "\t  ffmpeg2theora -f dv -x 160 -y 128 -o /dev/stdout - | \\\n"
        "\t  oggfwd iccast2server 8000 password /theora.ogg\n"
        "\n");
    exit (0);
}

int main (int argc, char **argv){
    int  n;
    int  outputfile_set=0;
    char outputfile_name[255];
    char inputfile_name[255];
    char *str_ptr;
    
    static int flag=-1;
    static int metadata_flag=0;

    AVInputFormat *input_fmt = NULL;
    AVFormatParameters *formatParams = NULL;
    
    int c,long_option_index;
    const char *optstring = "o:f:x:y:v:V:a:A:S:K:d:H:c:p:N:s:e:D:h::";
    struct option options [] = {
      {"output",required_argument,NULL,'o'},
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
      {"nosound",0,&flag,NOSOUND_FLAG},
      {"v4l",required_argument,&flag,V4L_FLAG},
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
                            convert->deinterlace=1;
                            flag=-1;
                            break;
                        case SYNC_FLAG:
                            convert->sync=1;
                            flag=-1;
                            break;
                        case NOSOUND_FLAG:
                            convert->disable_audio=1;
                            flag=-1;
                            break;
                        case V4L_FLAG:
                            formatParams = malloc(sizeof(AVFormatParameters));
                            formatParams->device = optarg;
                            flag=-1;
                            break;
                /* crop */
                        case CROPTOP_FLAG:
                            convert->frame_topBand=crop_check(convert,"top",optarg);
                            flag=-1;
                            break;
                        case CROPBOTTOM_FLAG:
                            convert->frame_bottomBand=crop_check(convert,"bottom",optarg);
                            flag=-1;
                            break;
                        case CROPRIGHT_FLAG:
                            convert->frame_rightBand=crop_check(convert,"right",optarg);
                            flag=-1;
                            break;
                        case CROPLEFT_FLAG:
                            convert->frame_leftBand=crop_check(convert,"left",optarg);
                            flag=-1;
                            break;
                        case ASPECT_FLAG:
                            convert->frame_aspect=aspect_check(optarg);
                            flag=-1;
                            break;
                        case INPUTFPS_FLAG:
                            convert->force_input_fps=atof(optarg);
                            flag=-1;
                            break;
                        case AUDIOSTREAM_FLAG:
                            convert->audiostream=atoi(optarg);;
                            flag=-1;
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
                sprintf(outputfile_name,optarg);
                outputfile_set=1;
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
                        fprintf(stderr,"only values from 0 to 10 are valid for video quality\n");
                        exit(1);
                }
                convert->video_bitrate=0;
                break;
            case 'V':
                convert->video_bitrate=rint(atof(optarg)*1000);
                if(convert->video_bitrate<0 || convert->video_bitrate>2000000){
                    fprintf(stderr,"only values from 0 to 2000000 are valid for video bitrate(in kbps)\n");
                    exit(1);
                }
                convert->video_quality=0;
                break; 
            case 'a':
                convert->audio_quality=atof(optarg)*.099;
                if(convert->audio_quality<-.2 || convert->audio_quality>1){
                    fprintf(stderr,"only values from -1 to 10 are valid for audio quality\n");
                    exit(1);
                }
                convert->audio_bitrate=0;
                break;
            case 'A':
                convert->audio_bitrate=atof(optarg)*1000;
                if(convert->audio_bitrate<0){
                    fprintf(stderr,"only values >0 are valid for audio bitrate\n");
                    exit(1);
                }
                convert->audio_quality=-99;
                break;
            case 'S':
                convert->sharpness = atoi(optarg);
                if (convert->sharpness < 0 || convert->sharpness > 2) {
                    fprintf (stderr, "only values from 0 to 2 are valid for sharpness\n");
                    exit(1);
                }
                break;
            case 'K':
                convert->keyint = atoi(optarg);
                if (convert->keyint < 8 || convert->keyint > 65536) {
                    fprintf (stderr, "only values from 8 to 65536 are valid for keyframe interval\n");
                    exit(1);
                }
                break;                        
            case 'H':
                convert->sample_rate=atoi(optarg);
                break;
            /* does this work? */
            case 'c':
                convert->channels=atoi(optarg);
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
                    convert->audio_quality=3*.099;
                    convert->channels=2;
                    convert->sample_rate=48000;
                    convert->sharpness=0;
                }
                else if(!strcmp(optarg,"preview")){
                    //need a way to set resize here. and not later
                    convert->preset=V2V_PRESET_PREVIEW;
                    convert->video_quality = rint(5*6.3);
                    convert->audio_quality=1*.099;
                    convert->channels=2;
                    convert->sample_rate=44100;
                    convert->sharpness=2;
                }
                else{
                    fprintf(stderr,"\nunknown preset.\n\n");
                    print_presets_info();
                    exit(1);
                }
                break;
            case 'N':
                n = atoi(optarg);
                if (n) {
#ifndef _WIN32
                    if (nice(n)<0) {
                        fprintf(stderr,"error setting %d for niceness", n);
                    }
#endif
                }
                break;
            case 'h':
                print_usage ();
                exit(1);
        }  
    }    
    //use PREVIEW as default setting
    if(argc==2){
        //need a way to set resize here. and not later
        convert->preset=V2V_PRESET_PREVIEW;
        convert->video_quality = rint(5*6.3);
        convert->audio_quality=1*.099;
        convert->channels=2;
        convert->sample_rate=44100;
    }
    
    while(optind<argc){
        /* assume that anything following the options must be a filename */
        sprintf(inputfile_name,"%s",argv[optind]);
        if(!strcmp(inputfile_name,"-")){
            sprintf(inputfile_name,"pipe:");
        }
        if(outputfile_set!=1){
            sprintf(outputfile_name, "%s", argv[optind]);
            if(str_ptr = rindex(outputfile_name, '.')) {
                sprintf(str_ptr, ".ogg");
            }
           else {
                sprintf(outputfile_name, "%s.ogg", outputfile_name);
            }
            outputfile_set=1;
        }
        optind++;
    }


    if(formatParams != NULL) {
        formatParams->channel = 0;
        formatParams->width = 384;
        formatParams->height = 288;
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

    //FIXME: is using_stdin still neded? is it needed as global variable?
    using_stdin |= !strcmp(inputfile_name, "pipe:" ) ||
                   !strcmp( inputfile_name, "/dev/stdin" );

    if(outputfile_set!=1){    
        fprintf(stderr,"you have to specifie an output file with -o output.ogg.\n");    
        exit(1);
    }

    /* could go, but so far no player supports offset_x/y */
    if(convert->picture_width % 8 ||  convert->picture_height % 8){
        fprintf(stderr,"output size must be a multiple of 8 for now.\n");
        exit(1);
    }
    /*
    if(convert->picture_width % 4 ||  convert->picture_height % 4){
        fprintf(stderr,"output width and hight size must be a multiple of 2.\n");
        exit(1);
    }
    */
    if(convert->end_time>0 && convert->end_time <= convert->start_time){
        fprintf(stderr,"end time has to be bigger than start time\n");
        exit(1);
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
                    sprintf(outputfile_name,"/dev/stdout");
                }
                info.outfile = fopen(outputfile_name,"wb");
#endif
                dump_format (convert->context, 0,inputfile_name, 0);
                if(convert->disable_audio){
                    fprintf(stderr,"  [audio disabled].\n");
                }
                if(convert->sync){
                    fprintf(stderr,"  Use A/V Sync from input container.\n");
                }

                convert->pts_offset = 
                    (double) convert->context->start_time / AV_TIME_BASE;
                ff2theora_output (convert);
                convert->audio_index =convert->video_index = -1;
            }
            else{
                fprintf (stderr,"Unable to decode input\n");
            }
            av_close_input_file (convert->context);
        }
        else{
            fprintf (stderr, "Unable to open file %s\n", inputfile_name);
        }
    ff2theora_close (convert);
    fprintf(stderr,"\n");
    return(0);
}
