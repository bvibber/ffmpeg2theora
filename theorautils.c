/*
 * theorautils.c - Ogg Theora abstraction layer
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

#include "theora/theora.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"

#include "theorautils.h"

FILE *outfile;
theoraframes_info info;
double timebase;



static double rint(double x)
{
  if (x < 0.0)
    return (double)(int)(x - 0.5);
  else
    return (double)(int)(x + 0.5);
}

void theoraframes_init (){
	info.audio_bytesout = 0;
	info.video_bytesout = 0;

	/* yayness.  Set up Ogg output stream */
	srand (time (NULL));
	ogg_stream_init (&info.vo, rand ());	
	
	if(!info.audio_only){
		ogg_stream_init (&info.to, rand ());	/* oops, add one ot the above */
		theora_encode_init (&info.td, &info.ti);
		theora_info_clear (&info.ti);
	}
	/* init theora done */

	/* initialize Vorbis too, if we have audio. */
	if(!info.video_only){
		vorbis_info_init (&info.vi);
		/* Encoding using a VBR quality mode.  */
		int ret =vorbis_encode_init_vbr (&info.vi, info.channels,info.frequency,info.vorbis_quality);

		if (ret){
			fprintf (stderr,
				 "The Vorbis encoder could not set up a mode according to\n"
				 "the requested quality or bitrate.\n\n");
			exit (1);
		}

		vorbis_comment_init (&info.vc);
		vorbis_comment_add_tag (&info.vc, "ENCODER",PACKAGE_STRING);
		/* set up the analysis state and auxiliary encoding storage */
		vorbis_analysis_init (&info.vd, &info.vi);
		vorbis_block_init (&info.vd, &info.vb);

	}
	/* audio init done */

	/* write the bitstream header packets with proper page interleave */

	/* first packet will get its own page automatically */
	if(!info.audio_only){
		theora_encode_header (&info.td, &info.op);
		ogg_stream_packetin (&info.to, &info.op);
		if (ogg_stream_pageout (&info.to, &info.og) != 1){
			fprintf (stderr, "Internal Ogg library error.\n");
			exit (1);
		}
		fwrite (info.og.header, 1, info.og.header_len, outfile);
		fwrite (info.og.body, 1, info.og.body_len, outfile);

		/* create the remaining theora headers */
		theora_comment_init (&info.tc);
		theora_encode_comment (&info.tc, &info.op);
		ogg_stream_packetin (&info.to, &info.op);
		theora_encode_tables (&info.td, &info.op);
		ogg_stream_packetin (&info.to, &info.op);
	}
	if(!info.video_only){
		ogg_packet header;
		ogg_packet header_comm;
		ogg_packet header_code;

		vorbis_analysis_headerout (&info.vd, &info.vc, &header,
					   &header_comm, &header_code);
		ogg_stream_packetin (&info.vo, &header);	/* automatically placed in its own
								 * page */
		if (ogg_stream_pageout (&info.vo, &info.og) != 1){
			fprintf (stderr, "Internal Ogg library error.\n");
			exit (1);
		}
		fwrite (info.og.header, 1, info.og.header_len, outfile);
		fwrite (info.og.body, 1, info.og.body_len, outfile);

		/* remaining vorbis header packets */
		ogg_stream_packetin (&info.vo, &header_comm);
		ogg_stream_packetin (&info.vo, &header_code);
	}

	/* Flush the rest of our headers. This ensures
	 * the actual data in each stream will start
	 * on a new page, as per spec. */
	while (1 && !info.audio_only){
		int result = ogg_stream_flush (&info.to, &info.og);
		if (result < 0){
			/* can't get here */
			fprintf (stderr, "Internal Ogg library error.\n");
			exit (1);
		}
		if (result == 0)
			break;
		fwrite (info.og.header, 1, info.og.header_len, outfile);
		fwrite (info.og.body, 1, info.og.body_len, outfile);
	}
	while (1 && !info.video_only){
		int result = ogg_stream_flush (&info.vo, &info.og);
		if (result < 0){
			/* can't get here */
			fprintf (stderr,
				 "Internal Ogg library error.\n");
			exit (1);
		}
		if (result == 0)
			break;
		fwrite (info.og.header, 1, info.og.header_len,outfile);
		fwrite (info.og.body, 1, info.og.body_len, outfile);
	}

}


/**	theora_add_video adds the video the the stream
		data hold the one frame in the format provided by img_convert */

int theoraframes_add_video (uint8_t * data, int width, int height, int linesize){
	/* map some things from info struk to local variables, 
	 * just to understand the code better */
	/* pysical pages */
	yuv_buffer yuv;
	/*      Theora is a one-frame-in,one-frame-out system; submit a frame
	 * for compression and pull out the packet */
	{
		yuv.y_width = width;
		yuv.y_height = height;
		yuv.y_stride = linesize;

		yuv.uv_width = width / 2;
		yuv.uv_height = height / 2;
		yuv.uv_stride = linesize / 2;

		yuv.y = data;
		yuv.u = data + width * height;
		yuv.v = data + width * height * 5 / 4;
	}

	theora_encode_YUVin (&info.td, &yuv);
	theora_encode_packetout (&info.td, 0, &info.op);
	ogg_stream_packetin (&info.to, &info.op);
	if(info.videoflag==0){
		if (ogg_stream_pageout (&info.to, &info.videopage) > 0){
			info.videoflag = 1;
			return 0;
		}
		if (ogg_stream_eos (&info.to)){
			info.videoflag = 0;
			return 0;
		}
	}
	info.videoflag = 0;
	return 0;
}

/** theora_add_audio: fill the audio buffer
	copy from the encoder_example.c need to be reworkes in order to work. */
int theoraframes_add_audio (int16_t * buffer, int bytes, int samples){
	int i, count = 0;
	float **vorbis_buffer;
	if (bytes <= 0 && samples <= 0){
		/* end of file.  this can be done implicitly, but it's
		 * easier to see here in non-clever fashion.  Tell the
		 * library we're at end of stream so that it can handle the
		 * last frame and mark end of stream in the output properly */
		vorbis_analysis_wrote (&info.vd, 0);
		fprintf (stderr, "no audio samples at all\n");
	}
	else{
		vorbis_buffer = vorbis_analysis_buffer (&info.vd, samples);
		/* uninterleave samples */
		for (i = 0; i < samples; i++){
			vorbis_buffer[0][i] = buffer[count++] / 32768.f;
			vorbis_buffer[1][i] = buffer[count++] / 32768.f;
		}
		vorbis_analysis_wrote (&info.vd, samples);
	}
	if (!info.audioflag){
		while (vorbis_analysis_blockout (&info.vd, &info.vb) == 1){
		/* analysis, assume we want to use bitrate management */
			vorbis_analysis (&info.vb, NULL);
			vorbis_bitrate_addblock (&info.vb);

		/* weld packets into the bitstream */
			while (vorbis_bitrate_flushpacket (&info.vd, &info.op))
				ogg_stream_packetin (&info.vo, &info.op);
		}
		if (ogg_stream_pageout (&info.vo, &info.audiopage) > 0){
			info.audioflag = 1;
			return 0;
		}
	}
	if (ogg_stream_eos (&info.vo)){
		info.audioflag = 0;
		return 0;
	}
	return 0;
}


void theoraframes_flush (){
	/* setup complete.  Raw processing loop */
	/* fprintf(stderr,"Compressing....\n"); */

	if (!info.audioflag && !info.videoflag)
		return;

	/* which is earlier; the end of the audio page or the end of the
	 * video page? Flush the earlier to stream */
	{
		int video = -1;
		if(info.audioflag)
			info.audiotime=vorbis_granule_time (&info.vd,ogg_page_granulepos(&info.audiopage));
		if(info.videoflag)
			info.videotime=theora_granule_time (&info.td,ogg_page_granulepos(&info.videopage));
		
		//some debuging 
		//fprintf(stderr,"\ndiff: %f\n",info.audiotime-info.videotime);

		if (info.videoflag){
			/* flush a video page */
			info.video_bytesout +=fwrite (info.videopage.header, 1,info.videopage.header_len, outfile);
			info.video_bytesout +=fwrite (info.videopage.body, 1,info.videopage.body_len, outfile);
			info.videoflag = 0;
			video=1;
			timebase = info.videotime;
		}
		if(info.audiotime < info.videotime && info.audioflag){
			/* flush an audio page */
			info.audio_bytesout +=fwrite (info.audiopage.header, 1,info.audiopage.header_len, outfile);
			info.audio_bytesout +=fwrite (info.audiopage.body, 1,info.audiopage.body_len, outfile);
			info.audioflag = 0;
			timebase = info.audiotime;
			video=0;
		}
		if(video>=0)
		{
			int hundredths = timebase * 100 - (long) timebase * 100;
			int seconds = (long) timebase % 60;
			int minutes = ((long) timebase / 60) % 60;
			int hours = (long) timebase / 3600;
			{
				if (video)
					info.vkbps = rint (info.video_bytesout *
						      8. / timebase * .001);
				else
					info.akbps = rint (info.audio_bytesout *
						      8. / timebase * .001);

				if(info.vkbps<0)
					info.vkbps=0;
				if(info.debug==1){
					fprintf (stderr,"\r      %d:%02d:%02d.%02d audio: %dkbps video: %dkbps diff: %.4f             ",
					 hours, minutes, seconds, hundredths,info.akbps, info.vkbps,info.audiotime-info.videotime);
				}
				else{
					fprintf (stderr,"\r      %d:%02d:%02d.%02d audio: %dkbps video: %dkbps                  ",
					 hours, minutes, seconds, hundredths,info.akbps, info.vkbps);
				}
			}
		}
	}
}


void theoraframes_close (){
	/* do we have to write last page do output file with EOS and stuff??*/
	/* pysical pages */
	
	ogg_stream_clear (&info.vo);
	vorbis_block_clear (&info.vb);
	vorbis_dsp_clear (&info.vd);
	vorbis_comment_clear (&info.vc);
	vorbis_info_clear (&info.vi);

	ogg_stream_clear (&info.to);
	theora_clear (&info.td);

	if (outfile && outfile != stdout)
		fclose (outfile);
}
