/*
 * theorautils.h -- ogg/theora Utils for ffmpeg2ogg
 * Copyright (C) 2003 <j@thing.net>
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

#define V2V_PRESET_PRO 1
#define V2V_PRESET_PREVIEW 2

typedef struct
{
	int debug;
	int preset;
	int audio_only;
	int video_only;
	int frequency;
	int channels;
	double vorbis_quality;
	int scale;
	int wide;
	double start;
	double end;
	int fps;
	ogg_page videopage;
	ogg_page audiopage;
	int audioflag;
	int videoflag;
	double audiotime;
	double videotime;
	double timebase;
	int vkbps;
	int akbps;
	ogg_int64_t audio_bytesout;
	ogg_int64_t video_bytesout;

	ogg_stream_state to;	/* take physical pages, weld into a logical
				 * stream of packets */
	ogg_stream_state vo;	/* take physical pages, weld into a logical
				 * stream of packets */
	ogg_page og;		/* one Ogg bitstream page.  Vorbis packets are inside */
	ogg_packet op;		/* one raw packet of data for decode */

	theora_info ti;
	theora_state td;
	theora_comment tc;

	vorbis_info vi;		/* struct that stores all the static vorbis bitstream settings */
	vorbis_comment vc;	/* struct that stores all the user comments */

	vorbis_dsp_state vd;	/* central working state for the packet->PCM decoder */
	vorbis_block vb;	/* local working space for packet->PCM decode */

}
theoraframes_info;


extern void theoraframes_init ();
extern int theoraframes_add_video (uint8_t * data, int width, int height,
				   int linesize);
extern int theoraframes_add_audio (int16_t * readbuffer, int bytesread,
				   int samplesread);
extern void theoraframes_flush ();
extern void theoraframes_close ();
