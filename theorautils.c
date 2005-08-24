/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * theorautils.c - Ogg Theora abstraction layer
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

void init_info(theoraframes_info *info) {
    info->videotime =  0;
    info->audiotime = 0;
    info->debug = 0;
    info->videopage_valid = 0;
    info->audiopage_valid = 0;
    info->audiopage_buffer_length = 0;
    info->videopage_buffer_length = 0;
    info->audiopage = NULL;
    info->videopage = NULL;
}


void theoraframes_init (theoraframes_info *info){
    ogg_page og;

    info->audio_bytesout = 0;
    info->video_bytesout = 0;

    /* yayness.  Set up Ogg output stream */
    srand (time (NULL));
    ogg_stream_init (&info->vo, rand ());    
    
    if(!info->audio_only){
        ogg_stream_init (&info->to, rand ());    /* oops, add one ot the above */
        theora_encode_init (&info->td, &info->ti);
        theora_info_clear (&info->ti);
    }
    /* init theora done */

    /* initialize Vorbis too, if we have audio. */
    if(!info->video_only){
        int ret;
        vorbis_info_init (&info->vi);
        /* Encoding using a VBR quality mode.  */
        if(info->vorbis_quality>-99) 
            ret =vorbis_encode_init_vbr (&info->vi, info->channels,info->sample_rate,info->vorbis_quality);
        else
            ret=vorbis_encode_init(&info->vi,info->channels,info->sample_rate,-1,info->vorbis_bitrate,-1); 

        if (ret){
            fprintf (stderr,
                 "The Vorbis encoder could not set up a mode according to\n"
                 "the requested quality or bitrate.\n\n");
            exit (1);
        }

        vorbis_comment_init (&info->vc);
        vorbis_comment_add_tag (&info->vc, "ENCODER",PACKAGE_STRING);
        /* set up the analysis state and auxiliary encoding storage */
        vorbis_analysis_init (&info->vd, &info->vi);
        vorbis_block_init (&info->vd, &info->vb);

    }
    /* audio init done */

    /* write the bitstream header packets with proper page interleave */

    /* first packet will get its own page automatically */
    if(!info->audio_only){
        theora_encode_header (&info->td, &info->op);
        ogg_stream_packetin (&info->to, &info->op);
        if (ogg_stream_pageout (&info->to, &og) != 1){
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        fwrite (og.header, 1, og.header_len, info->outfile);
        fwrite (og.body, 1, og.body_len, info->outfile);

        /* create the remaining theora headers */
        /* theora_comment_init (&info->tc); is called in main() prior to parsing options */
        theora_comment_add_tag (&info->tc, "ENCODER",PACKAGE_STRING);
        theora_encode_comment (&info->tc, &info->op);
        ogg_stream_packetin (&info->to, &info->op);
        theora_encode_tables (&info->td, &info->op);
        ogg_stream_packetin (&info->to, &info->op);
    }
    if(!info->video_only){
        ogg_packet header;
        ogg_packet header_comm;
        ogg_packet header_code;

        vorbis_analysis_headerout (&info->vd, &info->vc, &header,
                       &header_comm, &header_code);
        ogg_stream_packetin (&info->vo, &header);    /* automatically placed in its own
                                 * page */
        if (ogg_stream_pageout (&info->vo, &og) != 1){
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        fwrite (og.header, 1, og.header_len, info->outfile);
        fwrite (og.body, 1, og.body_len, info->outfile);

        /* remaining vorbis header packets */
        ogg_stream_packetin (&info->vo, &header_comm);
        ogg_stream_packetin (&info->vo, &header_code);
    }

    /* Flush the rest of our headers. This ensures
     * the actual data in each stream will start
     * on a new page, as per spec. */
    while (1 && !info->audio_only){
        int result = ogg_stream_flush (&info->to, &og);
        if (result < 0){
            /* can't get here */
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        if (result == 0)
            break;
        fwrite (og.header, 1, og.header_len, info->outfile);
        fwrite (og.body, 1, og.body_len, info->outfile);
    }
    while (1 && !info->video_only){
        int result = ogg_stream_flush (&info->vo, &og);
        if (result < 0){
            /* can't get here */
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        if (result == 0)
            break;
        fwrite (og.header, 1, og.header_len,info->outfile);
        fwrite (og.body, 1, og.body_len, info->outfile);
    }

}


/**    
 * adds a video frame to the encoding sink
 * if e_o_s is 1 the end of the logical bitstream will be marked.
 * @param this ff2theora struct 
 * @param info theoraframes_info
 * @param avframe AVFrame to encode
 * @param e_o_s 1 indicates ond of stream
 */
int theoraframes_add_video (ff2theora this, theoraframes_info *info, AVFrame * avframe, int e_o_s){
    /* pysical pages */
    yuv_buffer yuv;
    /* Theora is a one-frame-in,one-frame-out system; submit a frame
     * for compression and pull out the packet */
    {
        yuv.y_width = this->frame_width;
        yuv.y_height = this->frame_height;
        yuv.y_stride = avframe->linesize[0];

        yuv.uv_width = this->frame_width / 2;
        yuv.uv_height = this->frame_height / 2;
        yuv.uv_stride = avframe->linesize[1];

        yuv.y = avframe->data[0];
        yuv.u = avframe->data[1];
        yuv.v = avframe->data[2];

    }
    theora_encode_YUVin (&info->td, &yuv);
    while(theora_encode_packetout (&info->td, e_o_s, &info->op)) {
      ogg_stream_packetin (&info->to, &info->op);
    }
    return 0;
}
    
/** 
 * adds audio samples to encoding sink
 * @param buffer pointer to buffer
 * @param bytes bytes in buffer
 * @param samples samples in buffer
 * @param e_o_s 1 indicates end of stream.
 */
int theoraframes_add_audio (theoraframes_info *info, int16_t * buffer, int bytes, int samples, int e_o_s){
    int i,j, count = 0;
    float **vorbis_buffer;
    if (bytes <= 0 && samples <= 0){
        /* end of audio stream */
        if(e_o_s)
            vorbis_analysis_wrote (&info->vd, 0);
    }
    else{
        vorbis_buffer = vorbis_analysis_buffer (&info->vd, samples);
        /* uninterleave samples */
        for (i = 0; i < samples; i++){
            for(j=0;j<info->channels;j++){
                vorbis_buffer[j][i] = buffer[count++] / 32768.f;
            }
        }
        vorbis_analysis_wrote (&info->vd, samples);
    }
    while(vorbis_analysis_blockout (&info->vd, &info->vb) == 1){
        
        /* analysis, assume we want to use bitrate management */
        vorbis_analysis (&info->vb, NULL);
        vorbis_bitrate_addblock (&info->vb);
        
        /* weld packets into the bitstream */
        while (vorbis_bitrate_flushpacket (&info->vd, &info->op)){
            ogg_stream_packetin (&info->vo, &info->op);
        }

    }
    return 0;
}

static void print_stats(theoraframes_info *info, double timebase){
    int hundredths = timebase * 100 - (long) timebase * 100;
    int seconds = (long) timebase % 60;
    int minutes = ((long) timebase / 60) % 60;
    int hours = (long) timebase / 3600;

    if(info->vkbps<0)
        info->vkbps=0;
    if(info->akbps<0)
        info->akbps=0;

    if(info->debug==1 && !info->video_only && !info->audio_only){
        fprintf (stderr,"\r      %d:%02d:%02d.%02d audio: %dkbps video: %dkbps diff: %.4f             ",
         hours, minutes, seconds, hundredths,info->akbps, info->vkbps,info->audiotime-info->videotime);
    }
    else{
        fprintf (stderr,"\r      %d:%02d:%02d.%02d audio: %dkbps video: %dkbps                  ",
         hours, minutes, seconds, hundredths,info->akbps, info->vkbps);
    }

}

static int write_audio_page(theoraframes_info *info)
{
  /* TODO: Check return values */
  info->audio_bytesout +=
    fwrite(info->audiopage, 1, info->audiopage_len, info->outfile);
  info->audiopage_valid = 0;

  info->akbps = rint (info->audio_bytesout * 8. / info->audiotime * .001);
  print_stats(info, info->audiotime);
}

static int write_video_page(theoraframes_info *info)
{
  /* TODO: Check return values */
  info->video_bytesout +=
    fwrite(info->videopage, 1, info->videopage_len, info->outfile);
  info->videopage_valid = 0;

  info->vkbps = rint (info->video_bytesout * 8. / info->videotime * .001);
  print_stats(info, info->videotime);
}

void theoraframes_flush (theoraframes_info *info, int e_o_s)
{
    int len;
    ogg_page og;

    /* flush out the ogg pages to info->outfile */
    while(1) {
      /* Get pages for both streams, if not already present, and if available.*/
      if(!info->audio_only && !info->videopage_valid) {
        if(ogg_stream_pageout(&info->to, &og) > 0) {
          len = og.header_len + og.body_len;
          if(info->videopage_buffer_length < len) {
            info->videopage = realloc(info->videopage, len);
            info->videopage_buffer_length = len;
          }
          info->videopage_len = len;
          memcpy(info->videopage, og.header, og.header_len);
          memcpy(info->videopage+og.header_len , og.body, og.body_len);

          info->videopage_valid = 1;
          info->videotime = theora_granule_time (&info->td,
                  ogg_page_granulepos(&og));
        }
      }
      if(!info->video_only && !info->audiopage_valid) {
        if(ogg_stream_pageout(&info->vo, &og) > 0) {
          len = og.header_len + og.body_len;
          if(info->audiopage_buffer_length < len) {
            info->audiopage = realloc(info->audiopage, len);
            info->audiopage_buffer_length = len;
          }
          info->audiopage_len = len;
          memcpy(info->audiopage, og.header, og.header_len);
          memcpy(info->audiopage+og.header_len , og.body, og.body_len);

          info->audiopage_valid = 1;
          info->audiotime= vorbis_granule_time (&info->vd, 
                  ogg_page_granulepos(&og));
        }
      }

      if(info->video_only && info->videopage_valid) {
        write_video_page(info);
      }
      else if(info->audio_only && info->audiopage_valid) {
        write_audio_page(info);
      }
      /* We're using both. We can output only:
       *  a) If we have valid pages for both
       *  b) At EOS, for the remaining stream.
       */
      else if(info->videopage_valid && info->audiopage_valid) {
        /* Make sure they're in the right order. */
        if(info->videotime <= info->audiotime)
          write_video_page(info);
        else
          write_audio_page(info);
      } 
      else if(e_o_s && info->videopage_valid) {
          write_video_page(info);
      }
      else if(e_o_s && info->audiopage_valid) {
          write_audio_page(info);
      }
      else {
        break; /* Nothing more writable at the moment */
      }
    }
}

void theoraframes_close (theoraframes_info *info){
    ogg_stream_clear (&info->vo);
    vorbis_block_clear (&info->vb);
    vorbis_dsp_clear (&info->vd);
    vorbis_comment_clear (&info->vc);
    vorbis_info_clear (&info->vi);

    ogg_stream_clear (&info->to);
    theora_clear (&info->td);

    if (info->outfile && info->outfile != stdout)
        fclose (info->outfile);

    if(info->videopage)
      free(info->videopage);
    if(info->audiopage)
      free(info->audiopage);
}
