/*
 * ffmpeg2theora.c -- Convert ffmpeg supported a/v files to  Ogg Theora
 * Copyright (C) 2003-2004 <j@thing.net>
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

#include "theorautils.h"

static double rint(double x)
{
  if (x < 0.0)
    return (double)(int)(x - 0.5);
  else
    return (double)(int)(x + 0.5);
}

FILE *outfile;
theoraframes_info info;
static int using_stdin = 0;

typedef struct ff2theora{
	AVFormatContext *context;
	int video_index;
	int audio_index;
	int deinterlace;
	int frequency;
	int channels;
	int disable_audio;
	float audio_quality;
	int output_width;
	int output_height;
	double fps;
	ImgReSampleContext *img_resample_ctx; /* for image resampling/resizing */
	ReSampleContext *audio_resample_ctx;
	double aspect_numerator;
	double aspect_denominator;
	int video_quality;
}
*ff2theora;

/* Allocate and initialise an AVFrame. */
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

ff2theora ff2theora_init (){
	ff2theora this = calloc (1, sizeof (*this));
	if (this != NULL){
		// initialise with sane values here.
		this->disable_audio=0;
		this->video_index = -1;
		this->audio_index = -1;
		this->frequency = 44100;  // samplerate hmhmhm
		this->channels = 2;
		this->output_width=0;	  // set to 0 to not resize the output
		this->output_height=0;	  // set to 0 to not resize the output
		this->video_quality=31.5; // video quality 5
		this->audio_quality=0.297;// audio quality 3
		this->aspect_numerator=0;
		this->aspect_denominator=0;
		this->deinterlace=1;
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
#ifdef FFMPEGCVS
		if(venc->sample_aspect_ratio.num!=0){
			//the way vlc is doing it right now.
			//this->aspect_numerator=venc->sample_aspect_ratio.num*(float)venc->width;
			//this->aspect_denominator=venc->sample_aspect_ratio.den*(float)venc->height;
			this->aspect_numerator=venc->sample_aspect_ratio.num;
			this->aspect_denominator=venc->sample_aspect_ratio.den;
			//fprintf(stderr,"  Aspect %.2f/1\n",this->aspect_numerator/this->aspect_denominator);
		}
#else
		if(venc->aspect_ratio!=0){
			//this->aspect_numerator=venc->aspect_ratio*10000;
			//this->aspect_denominator=10000;
			this->aspect_numerator=(venc->aspect_ratio/(float)venc->width)*10000;
			this->aspect_denominator=10000/(float)venc->height;
			//fprintf(stderr,"  Aspect %.2f/1\n",this->aspect_numerator/this->aspect_denominator);
		}
		
#endif
		if (fps > 10000)
			fps /= 1000;
		if (vcodec == NULL || avcodec_open (venc, vcodec) < 0)
			this->video_index = -1;
		this->fps = fps;

		if(this->output_height>0){
			// we might need that for values other than /16?
			int frame_topBand=0;
			int frame_bottomBand=0;
			int frame_leftBand=0;
			int frame_rightBand=0;
			int frame_padtop=0, frame_padbottom=0;
			int frame_padleft=0, frame_padright=0;
			/* ffmpeg cvs version */
#ifdef FFMPEGCVS
			this->img_resample_ctx = img_resample_full_init(
                                      this->output_width, this->output_height,
                                      venc->width, venc->height,
                                      frame_topBand, frame_bottomBand,
                                      frame_leftBand, frame_rightBand,
									  frame_padtop, frame_padbottom,
									  frame_padleft, frame_padright
				      );
#else
			/* ffmpeg 0.48 */
			this->img_resample_ctx = img_resample_full_init(
                                      this->output_width, this->output_height,
                                      venc->width, venc->height,
                                      frame_topBand, frame_bottomBand,
                                      frame_leftBand, frame_rightBand
				      );
#endif
			fprintf(stderr,"  Resize: %dx%d => %dx%d\n",venc->width,venc->height,this->output_width,this->output_height);
		}
		else{
			this->output_height=venc->height;
			this->output_width=venc->width;
		}

	}

	if (this->audio_index >= 0){
		astream = this->context->streams[this->audio_index];
		aenc = &this->context->streams[this->audio_index]->codec;
		acodec = avcodec_find_decoder (aenc->codec_id);
		if (this->channels != aenc->channels && aenc->codec_id == CODEC_ID_AC3)
			aenc->channels = this->channels;
		if (acodec != NULL && avcodec_open (aenc, acodec) >= 0)
			if(this->frequency!=aenc->sample_rate || this->channels!=aenc->channels){
				this->audio_resample_ctx = audio_resample_init (this->channels,aenc->channels,this->frequency,aenc->sample_rate);
				fprintf(stderr,"  Resample: %dHz => %dHz\n",aenc->sample_rate,this->frequency);
			}
			else{
				this->audio_resample_ctx=NULL;
			}
		
		else
			this->audio_index = -1;
	}

	if (this->video_index >= 0 || this->audio_index >=0){
		AVFrame *frame=NULL;
		AVFrame *frame_tmp=NULL;
		AVFrame *output=NULL;
		AVFrame *output_tmp=NULL;
		AVFrame *output_resized=NULL;
		
		AVPacket pkt;
		int len;
		int len1;
		int got_picture;
		int first = 1;
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
		
		int ret;
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
							this->output_width,this->output_height);
		}

		if(!info.audio_only){
			/* video settings here */
			/*  config file? commandline options? v2v presets? */
			theora_info_init (&info.ti);
			info.ti.width = this->output_width;
			info.ti.height = this->output_height;
			info.ti.frame_width = this->output_width;
			info.ti.frame_height = this->output_height;
			info.ti.offset_x = 0;
			info.ti.offset_y = 0;
			info.ti.fps_numerator = 1000000 * (this->fps);	/* fps= numerator/denominator */
			info.ti.fps_denominator = 1000000;
			/*
			DV NTSC
			4:3  	10:11  	720x480
			16:9 	40:33 	720x480
			DV PAL
			4:3  	59:54  	720x576
			16:9 	118:81 	720x576
			---
			so this is the aspect ratio of the frame 4/3, 16/9 etc */
			info.ti.aspect_numerator=this->aspect_numerator;
			info.ti.aspect_denominator=this->aspect_denominator;
			if(this->fps==25)
				info.ti.colorspace = OC_CS_ITU_REC_470BG;
			else if(abs(this->fps-30)<1)
				info.ti.colorspace = OC_CS_ITU_REC_470M;
			else
				info.ti.colorspace = OC_CS_UNSPECIFIED;
			//info.ti.target_bitrate=1200; 
			info.ti.quality = this->video_quality;
			info.ti.dropframes_p = 0;
			info.ti.quick_p = 1;
			info.ti.keyframe_auto_p = 1;
			info.ti.keyframe_frequency = 64;
			info.ti.keyframe_frequency_force = 64;
			//info.ti.keyframe_data_target_bitrate = info.ti.target_bitrate * 1.5;
			info.ti.keyframe_auto_threshold = 80;
			info.ti.keyframe_mindistance = 8;
			info.ti.noise_sensitivity = 1;
		}
		/* audio settings here */
		info.channels = this->channels;
		info.frequency = this->frequency;
		info.vorbis_quality = this->audio_quality;
		theoraframes_init ();
		
		do{
			ret = av_read_packet (this->context, &pkt);
			
			ptr = pkt.data;
			len = pkt.size;

			if (ret >= 0 && pkt.stream_index == this->video_index){
	
				if(len == 0 && !first){
					fprintf (stderr, "no frame available\n");
				}
				while(len > 0){
					len1 = avcodec_decode_video(&vstream->codec, frame,&got_picture, ptr, len);
					if(len1 < 0)
						break;

					if(got_picture){
						/* might have to cast other progressive formats here */
						//if(venc->pix_fmt != PIX_FMT_YUV420P){
							img_convert((AVPicture *)output,PIX_FMT_YUV420P,
										(AVPicture *)frame,venc->pix_fmt,
										venc->width,venc->height);
							if(this->deinterlace){
								if(avpicture_deinterlace((AVPicture *)output_tmp,
											(AVPicture *)output,PIX_FMT_YUV420P
											,venc->width,venc->height)<0){
									output_tmp = output;
								}
							}
							else
								output_tmp = output;
							
						//}
						//else{
							/* there must be better way to do this, it seems to work like this though */
						/*
							if(frame->linesize[0] != vstream->codec.width){
								img_convert((AVPicture *)output_tmp,PIX_FMT_YUV420P,
											(AVPicture *)frame,venc->pix_fmt,venc->width,venc->height);
							}
							else{
								output_tmp=frame;
							}
						}
						*/
						// now output_tmp
						if(this->img_resample_ctx){
							img_resample(this->img_resample_ctx, 
										(AVPicture *)output_resized, (AVPicture *)output_tmp);
						}	
						else{
							output_resized=output_tmp;
						}
						// now output_resized
						first = 0;
						if(theoraframes_add_video(output_resized->data[0],
							this->output_width,this->output_height,output_resized->linesize[0])){
							ret = -1;
							fprintf (stderr,"No theora frames available\n");
							break;
						}
					}
					ptr += len1;
					len -= len1;
				}
			}
			else if(ret >= 0 && pkt.stream_index == this->audio_index){
				int data_size;
				while(len > 0 ){
					len1 = avcodec_decode_audio(&astream->codec, audio_buf,&data_size, ptr, len);
					if (len1 < 0){
                		/* if error, we skip the frame */
                		break;
            		}
					len -= len1;
					ptr += len1;
					if(data_size >0){
						int samples =data_size / (aenc->channels * 2);
						int samples_out = samples;
						if(this->audio_resample_ctx)
							samples_out = audio_resample(this->audio_resample_ctx, resampled, audio_buf, samples);
						else
							resampled=audio_buf;
						
						if (theoraframes_add_audio(resampled, samples_out *(aenc->channels),samples_out)){
							ret = -1;
							fprintf (stderr,"No audio frames available\n");
							break;
						}
					}
				}
			}
			/* flush out the file */
			theoraframes_flush ();
			av_free_packet (&pkt);
		}
		while (ret >= 0);
			
		av_free(audio_buf);

		if (this->img_resample_ctx)
		    img_resample_close(this->img_resample_ctx);
		if (this->audio_resample_ctx)
		    audio_resample_close(this->audio_resample_ctx);

		av_free(resampled);
		theoraframes_close ();
	}
	else{
		fprintf (stderr, "No video or audio stream found\n");
	}
}

void ff2theora_close (ff2theora this){
	/* clear out state */
	av_free (this);
}

void print_usage (){
	fprintf (stderr, 
		PACKAGE " " PACKAGE_VERSION "\n\n"
		" usage: " PACKAGE " [options] input\n\n"
		" Options:\n"
		"\t --output,-o alternative output\n"
		"\t --format,-f specify input format\n"
		"\t --width, -x scale to given size\n"
		"\t --height,-y scale to given size\n"
		"\t --videoquality,-v encoding quality for video ( 1 to 10)\n"
		"\t --audioquality,-a encoding quality for audio (-1 to 10)\n"
		"\t --deinterlace [off|on] 	disable deinterlace, enabled by default right now\n"
		"\t --samplerate 	set output samplerate in Hz\n"
		"\t --nosound,-n disable the sound from input, generate video only file\n"
		"\t --help,-h this message\n"
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
	int  outputfile_set=0;
	char outputfile_name[255];
	char inputfile_name[255];
	AVInputFormat *input_fmt=NULL;
	ff2theora convert = ff2theora_init ();
	av_register_all ();
	
	int c,long_option_index;
	const char *optstring = "o:f:x:y:v:a:n:h::";
	struct option options [] = {
	  {"output",required_argument,NULL,'o'},
	  {"format",required_argument,NULL,'f'},
	  {"width",required_argument,NULL,'x'},
	  {"height",required_argument,NULL,'y'},
	  {"videoquality",required_argument,NULL,'v'},
	  {"audioquality",required_argument,NULL,'a'},
	  {"deinterlace",required_argument,NULL,'deint'},
	  {"samplerate",required_argument,NULL,'H'},
	  {"channels",required_argument,NULL,'c'},
	  {"nosound",required_argument,NULL,'n'},
	  {"help",NULL,NULL,'h'},
	  {NULL,0,NULL,0}
	};
	if (argc == 1){
		print_usage ();
	}
	
	while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF){
		switch(c)
	    {
			case 'o':
				sprintf(outputfile_name,optarg);
				outputfile_set=1;
				break;
			case 'f':
				input_fmt=av_find_input_format(optarg);
				break;
			case 'x':
				convert->output_width=atoi(optarg);
				break;
			case 'y':
				convert->output_height=atoi(optarg);
				break;
			case 'v':
				convert->video_quality = rint(atof(optarg)*6.3);
				if(convert->video_quality <0 || convert->video_quality >63){
				        fprintf(stderr,"video quality out of range (choose 0 through 10)\n");
				        exit(1);
				}
				break;
			case 'a':
				convert->audio_quality=atof(optarg)*.099;
      				if(convert->audio_quality<-.1 || convert->audio_quality>1){
        				fprintf(stderr,"audio quality out of range (choose -1 through 10)\n");
        				exit(1);
				}
				break;
			case 'H':
				convert->frequency=atoi(optarg);
				break;
			/* does not work right now */
			case 'c':
				convert->channels=2;
				fprintf(stderr,"\n\tonly stereo works right now, encoding in stereo!\n\n");
				//convert->channels=atoi(optarg);
				break;
			case 'deint':
				if(!strcmp(optarg,"off"))
					convert->deinterlace=0;
				else
					convert->deinterlace=1;
				break;
			case 'n':
				convert->disable_audio=1;
				break;
			case 'h':
				print_usage ();
				exit(1);
		}  
	}	
	
	while(optind<argc){
		/* assume that anything following the options must be a filename */
		if(!strcmp(argv[optind],"-")){
			sprintf(inputfile_name,"pipe:");
		}
		else{
			sprintf(inputfile_name,"%s",argv[optind]);
			if(outputfile_set!=1){
				sprintf(outputfile_name,"%s.ogg",argv[optind]);
				outputfile_set=1;
			}	
		}
		optind++;
	}
	using_stdin |= !strcmp(inputfile_name, "pipe:" ) ||
                   !strcmp( inputfile_name, "/dev/stdin" );

	if(outputfile_set!=1){	
		fprintf(stderr,"you have to specifie an output file with -o output.ogg.\n");	
		exit(1);
	}
	
	if(convert->output_width % 16 ||  convert->output_height % 16){
		fprintf(stderr,"output size must be a multiple of 16 for now.\n");
		exit(1);
	}

	if (av_open_input_file(&convert->context, inputfile_name, input_fmt, 0, NULL) >= 0){
			outfile = fopen(outputfile_name,"wb");
			if (av_find_stream_info (convert->context) >= 0){
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
