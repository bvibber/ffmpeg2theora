/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * ffmpeg2theora.c -- Convert ffmpeg supported a/v files to  Ogg Theora
 * Copyright (C) 2003-2004 <j@v2v.cc>
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

static double rint(double x)
{
    if (x < 0.0)
        return (double)(int)(x - 0.5);
    else
        return (double)(int)(x + 0.5);
}

theoraframes_info info;

static int using_stdin = 0;


/**
 * Allocate and initialise an AVFrame. 
 */
AVFrame *alloc_picture (int pix_fmt, int width, int height){
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
        // video
        this->picture_width=0;      // set to 0 to not resize the output
        this->picture_height=0;      // set to 0 to not resize the output
        this->video_quality=31.5; // video quality 5
        this->video_bitrate=0;
        this->sharpness=2;
        this->keyint=64;
        this->force_input_fps=0;
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
    int64_t frame_number=0;
    double fps = 0.0;

    for (i = 0; i < this->context->nb_streams; i++){
        AVCodecContext *enc = &this->context->streams[i]->codec;
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
        venc = &this->context->streams[this->video_index]->codec;
        vcodec = avcodec_find_decoder (venc->codec_id);

        fps = (double) venc->frame_rate / venc->frame_rate_base;
        if (fps > 10000)
            fps /= 1000;

        if(this->force_input_fps)
            fps=this->force_input_fps;

        if (vcodec == NULL || avcodec_open (venc, vcodec) < 0)
            this->video_index = -1;

        this->fps = fps;
        
        if(info.preset == V2V_PRESET_PREVIEW){
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
        else if(info.preset == V2V_PRESET_PRO){
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
        else
            fprintf(stderr,"  Deinterlace: off\n");

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
        this->frame_width = this->picture_width + (this->picture_width % 16);        
        this->frame_height = this->picture_height + (this->picture_height % 16);

        this->frame_x_offset = 0;
        this->frame_y_offset = 0;
        
        if(this->frame_width > 0 || this->frame_height > 0){
            int frame_padtop = this->frame_width-this->picture_width;
            int frame_padbottom = this->frame_y_offset;
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
        aenc = &this->context->streams[this->audio_index]->codec;
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

        if(this->video_index >= 0)
            info.audio_only=0;
        else
            info.audio_only=1;
        
        if(this->audio_index>=0)
            info.video_only=0;
        else
            info.video_only=1;
        
        if(!info.audio_only){
            frame = alloc_picture(vstream->codec.pix_fmt,
                            vstream->codec.width,vstream->codec.height);
            frame_tmp = alloc_picture(vstream->codec.pix_fmt,
                            vstream->codec.width,vstream->codec.height);
            output_tmp =alloc_picture(PIX_FMT_YUV420P, 
                            vstream->codec.width,vstream->codec.height);
            output =alloc_picture(PIX_FMT_YUV420P, 
                            vstream->codec.width,vstream->codec.height);
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
                info.ti.fps_numerator=venc->frame_rate;
                info.ti.fps_denominator = venc->frame_rate_base;
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
        theoraframes_init (&info);
        /*seek to start time*/    
#if LIBAVFORMAT_BUILD <= 4616
        av_seek_frame( this->context, -1, (int64_t)AV_TIME_BASE*this->start_time);
#else
        av_seek_frame( this->context, -1, (int64_t)AV_TIME_BASE*this->start_time, 1);
#endif
        /*check for end time and caclulate number of frames to encode*/
        int no_frames = fps*(this->end_time - this->start_time);
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
                if(frame_number > no_frames){
                    if(info.debug)
                        fprintf(stderr,"\nreached end specified with --endtime\n");
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
                    
                    if(len >0 &&
                        (len1 = avcodec_decode_video(&vstream->codec,
                                        frame,&got_picture, ptr, len))>0) {
                                        
                        if(got_picture){
                            //For audio only files command line option"-e" will not work
                            //as we donot increment frame_number in audio section.
                            frame_number++;
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
                        ptr += len1;
                        len -= len1;
                    }    
                    first=0;
                    //now output_resized
                    if( theoraframes_add_video(this, &info, output_resized ,e_o_s) ){
                        ret = -1;
                        fprintf (stderr,"No theora frames available\n");
                        break;
                    }
                    if(e_o_s){
                        break;
                    }
                }
                
            }
            if(e_o_s && !info.video_only 
                     || (ret >= 0 && pkt.stream_index == this->audio_index)){
                while(e_o_s || len > 0 ){
                    int samples=0;
                    int samples_out=0;
                    int data_size;
                    if(len > 0){
                        len1 = avcodec_decode_audio(&astream->codec, audio_buf,&data_size, ptr, len);
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
                    if (theoraframes_add_audio(&info, resampled, 
                        samples_out *(this->channels),samples_out,e_o_s)){
                        ret = -1;
                        fprintf (stderr,"No audio frames available\n");
                    }
                    if(e_o_s && len <= 0){
                        break;
                    }
                }

            }
            /* flush out the file */
            theoraframes_flush (&info, e_o_s);
            av_free_packet (&pkt);
        }
        while (ret >= 0);

        if(audio_buf){
            if(audio_buf!=resampled)
                av_free(resampled);
            av_free(audio_buf);
        }
        
        if (this->img_resample_ctx)
            img_resample_close(this->img_resample_ctx);
        if (this->audio_resample_ctx)
            audio_resample_close(this->audio_resample_ctx);

        theoraframes_close (&info);
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
    fprintf (stderr, 
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
        "\t --channels,-c\tset number of output sound channels\n"
        "\t --nosound\t\tdisable the sound from input\n"
        "\t --endtime,-e\t\tend encoding at this time (in sec)\n"
        "\t --starttime,-s\t\tstart encoding at this time (in sec)\n"
        "\t --v2v-preset,-p\tencode file with v2v preset, \n"
        "\t\t\t\t right now there is preview and pro,\n"
        "\t\t\t\t '"PACKAGE" -p info' for more informations\n"
    
        "\nInput options:\n"
        "\t --deinterlace,-d \tforce deinterlace,n"
        "\t\t\t\t\t otherwise only interlaced material is deinterlaced\\n"
        "\t --format,-f\t\tspecify input format\n"
        "\t --inputfps [fps]\toverride input fps\n"
        
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
        "\t --debug\t\toutput some more information during encoding\n"
        "\t --help,-h\t\tthis message\n"


        "\n Examples:\n"
    
        "\tffmpeg2theora videoclip.avi (will write output to videoclip.avi.ogg)\n\n"
        "\tcat something.dv | ffmpeg2theora -f dv -o output.ogg -\n\n"
        "\tLive encoding from a DV camcorder (needs a fast machine)\n"
        "\tdvgrab - | ffmpeg2theora -f dv -x 352 -y 288 -o output.ogg -\n"
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
    
    static int croptop_flag=0;
    static int cropbottom_flag=0;
    static int cropright_flag=0;
    static int cropleft_flag=0;    
    static int nosound_flag=0;    
    static int aspect_flag=0;
    static int inputfps_flag=0;
    static int metadata_flag=0;
    static int deinterlace_flag=0;
    
    AVInputFormat *input_fmt=NULL;
    ff2theora convert = ff2theora_init ();
    av_register_all ();
    
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
      {"deinterlace",0,&deinterlace_flag,'d'},
      {"samplerate",required_argument,NULL,'H'},
      {"channels",required_argument,NULL,'c'},
      {"nosound",0,&nosound_flag,1},
      {"aspect",required_argument,&aspect_flag,1},
      {"v2v-preset",required_argument,NULL,'p'},
      {"nice",required_argument,NULL,'N'},
      {"croptop",required_argument,&croptop_flag,1},
      {"cropbottom",required_argument,&cropbottom_flag,1},
      {"cropright",required_argument,&cropright_flag,1},
      {"cropleft",required_argument,&cropleft_flag,1},
      {"inputfps",required_argument,&inputfps_flag,1},
      {"starttime",required_argument,NULL,'s'},
      {"endtime",required_argument,NULL,'e'},

      {"artist",required_argument,&metadata_flag,10},
      {"title",required_argument,&metadata_flag,11},
      {"date",required_argument,&metadata_flag,12},
      {"location",required_argument,&metadata_flag,13},
      {"organization",required_argument,&metadata_flag,14},
      {"copyright",required_argument,&metadata_flag,15},
      {"license",required_argument,&metadata_flag,16},

      {"debug",0,NULL,'D'},
      {"help",0,NULL,'h'},
      {NULL,0,NULL,0}
    };
    if (argc == 1){
        print_usage ();
    }
    // set some variables;
    info.debug=0;
    theora_comment_init (&info.tc);
    
    while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF){
        switch(c)
        {
            case 0:
                if (nosound_flag){
                    convert->disable_audio=1;
                    nosound_flag=0;
                }
                if (deinterlace_flag){
                    convert->deinterlace=1;
                    deinterlace_flag=0;
                }
                /* crop */
                if (croptop_flag){
                    convert->frame_topBand=crop_check(convert,"top",optarg);
                    croptop_flag=0;
                }
                if (cropbottom_flag){
                    convert->frame_bottomBand=crop_check(convert,"bottom",optarg);
                    cropbottom_flag=0;
                }
                if (cropright_flag){
                    convert->frame_rightBand=crop_check(convert,"right",optarg);
                    cropright_flag=0;
                }
                if (cropleft_flag){
                    convert->frame_leftBand=crop_check(convert,"left",optarg);
                    cropleft_flag=0;
                }
                if (aspect_flag){
                    convert->frame_aspect=aspect_check(optarg);
                    aspect_flag=0;
                }
                if (inputfps_flag){
                    convert->force_input_fps=atof(optarg);
                    inputfps_flag=0;
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
                if(convert->audio_quality<-.1 || convert->audio_quality>1){
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
            case 'd':
                if(!strcmp(optarg,"off"))
                    convert->deinterlace=0;
                else
                    convert->deinterlace=1;
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
                    info.preset=V2V_PRESET_PRO;
                    convert->video_quality = rint(7*6.3);
                    convert->audio_quality=3*.099;
                    convert->channels=2;
                    convert->sample_rate=48000;
                    convert->sharpness=0;
                }
                else if(!strcmp(optarg,"preview")){
                    //need a way to set resize here. and not later
                    info.preset=V2V_PRESET_PREVIEW;
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
            case 'D':
                //enable debug informations
                info.debug=1;
                break;
            case 'h':
                print_usage ();
                exit(1);
        }  
    }    
    //use PREVIEW as default setting
    if(argc==2){
        //need a way to set resize here. and not later
        info.preset=V2V_PRESET_PREVIEW;
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
            sprintf(outputfile_name,"%s.ogg",argv[optind]);
            outputfile_set=1;
        }
        optind++;
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

    if (av_open_input_file(&convert->context, inputfile_name, input_fmt, 0, NULL) >= 0){
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
