/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * theorautils.c - Ogg Theora/Ogg Vorbis Abstraction and Muxing
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

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_LARGEFILE_SOURCE)
#define _LARGEFILE_SOURCE
#endif
#if !defined(_LARGEFILE64_SOURCE)
#define _LARGEFILE64_SOURCE
#endif
#if !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#ifndef WIN32
#if !defined(off64_t)
#define off64_t off_t
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <assert.h>
#include <math.h>
#include <limits.h>

#ifdef WIN32
#if !defined(fseeko)
#define fseeko fseeko64
#define ftello ftello64
#endif
#endif

#include "theora/theoraenc.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"
#ifdef HAVE_OGGKATE
#include "kate/oggkate.h"
#endif

#include "theorautils.h"


void init_info(oggmux_info *info) {
    info->output_seekable = MAYBE_SEEKABLE;
    info->with_skeleton = 1; /* skeleton is enabled by default    */
    info->skeleton_3 = 0; /* by default, output skeleton 4 with keyframe indexes. */
    info->index_interval = 2000;
    info->theora_index_reserve = -1;
    info->vorbis_index_reserve = -1;
    info->kate_index_reserve = -1;
    info->indexing_complete = 0;
    info->frontend = NULL; /*frontend mode*/
    info->videotime =  0;
    info->audiotime = 0;
    info->audio_bytesout = 0;
    info->video_bytesout = 0;
    info->kate_bytesout = 0;

    info->videopage_valid = 0;
    info->audiopage_valid = 0;
    info->audiopage_buffer_length = 0;
    info->videopage_buffer_length = 0;
    info->audiopage = NULL;
    info->videopage = NULL;
    info->start_time = time(NULL);
    info->duration = -1;
    info->speed_level = -1;

    info->v_pkg=0;
    info->a_pkg=0;
    info->k_pkg=0;
#ifdef OGGMUX_DEBUG
    info->a_page=0;
    info->v_page=0;
    info->k_page=0;
#endif

    info->twopass_file = NULL;
    info->twopass = 0;
    info->passno = 0;

    info->with_kate = 0;
    info->n_kate_streams = 0;
    info->kate_streams = NULL;

    info->prev_vorbis_window = -1;
    info->content_offset = 0;

    info->serialno = 0;
}

void oggmux_setup_kate_streams(oggmux_info *info, int n_kate_streams)
{
    int n;

    info->n_kate_streams = n_kate_streams;
    info->kate_streams = NULL;
    if (n_kate_streams == 0) return;
    info->kate_streams = (oggmux_kate_stream*)malloc(n_kate_streams*sizeof(oggmux_kate_stream));
    for (n=0; n<n_kate_streams; ++n) {
        oggmux_kate_stream *ks=info->kate_streams+n;
        ks->katepage_valid = 0;
        ks->katepage_buffer_length = 0;
        ks->katepage = NULL;
        ks->katetime = 0;
        ks->last_end_time = -1;
    }
}

static void write16le(unsigned char *ptr,ogg_uint16_t v)
{
    ptr[0]=v&0xff;
    ptr[1]=(v>>8)&0xff;
}

static void write32le(unsigned char *ptr,ogg_uint32_t v)
{
    ptr[0]=v&0xff;
    ptr[1]=(v>>8)&0xff;
    ptr[2]=(v>>16)&0xff;
    ptr[3]=(v>>24)&0xff;
}

static void write64le(unsigned char *ptr,ogg_int64_t v)
{
    ogg_uint32_t hi=v>>32;
    ptr[0]=v&0xff;
    ptr[1]=(v>>8)&0xff;
    ptr[2]=(v>>16)&0xff;
    ptr[3]=(v>>24)&0xff;
    ptr[4]=hi&0xff;
    ptr[5]=(hi>>8)&0xff;
    ptr[6]=(hi>>16)&0xff;
    ptr[7]=(hi>>24)&0xff;
}

/* Write an ogg page to the output file. The first time this is called, we
   determine the seekable-ness of the output stream, and store the result
   in info->output_seekable. */
static void
write_page(oggmux_info* info, ogg_page* page)
{
    int x;
    assert(page->header_len > 0);
    x = fwrite(page->header, 1, page->header_len, info->outfile);
    if (x != page->header_len) {
        fprintf(stderr, "FAILURE: Failed to write page header to disk!\n");
        exit(1);
    }
    x = fwrite(page->body, 1, page->body_len, info->outfile);
    if (x != page->body_len) {
        fprintf(stderr, "FAILURE: Failed to write page body to disk!\n");
        exit(1);
    }
    if (info->output_seekable == MAYBE_SEEKABLE) {
        /* This is our first page write. Determine if the output
           is seekable. */
        ogg_int64_t offset = ftello(info->outfile);
        if (offset == -1 || fseeko(info->outfile, 0, SEEK_SET) < 0) {
            info->output_seekable = NOT_SEEKABLE;
        } else {
            /* Output appears to be seekable, seek the write cursor back
               to previous position. */
            info->output_seekable = SEEKABLE;
            assert(info->output_seekable > 0);
            if (fseeko(info->outfile, offset, SEEK_SET) < 0) {
                fprintf(stderr, "ERROR: failed to seek in seekable output file!?!\n");
                exit (1);
            }  
        }
    }
    /* We should know the seekableness by now... */
    assert(info->output_seekable != MAYBE_SEEKABLE);
}

static ogg_int64_t output_file_length(oggmux_info* info)
{
    ogg_int64_t offset, length;
    if (info->skeleton_3 || !info->indexing_complete) {
        return -1;
    }
    offset = ftello(info->outfile);
    if (fseeko(info->outfile, 0, SEEK_END) < 0) {
        fprintf(stderr, "ERROR: Can't seek output file to write index!\n");
        return -1;
    }
    length = ftello(info->outfile);
    if (fseeko(info->outfile, offset, SEEK_SET) < 0) {
        fprintf(stderr, "ERROR: Can't seek output file to write index!\n");
        return -1;
    }

    return length;
}


void add_fishead_packet (oggmux_info *info,
                         ogg_uint16_t ver_maj,
                         ogg_uint16_t ver_min) {
    ogg_packet op;
    size_t packet_size = 0;
    ogg_uint32_t version = SKELETON_VERSION(ver_maj, ver_min);

    assert(version >= SKELETON_VERSION(3,0) ||
           version == SKELETON_VERSION(4,0));

    packet_size = (version == SKELETON_VERSION(4,0)) ? 80 : 64;

    memset (&op, 0, sizeof (op));

    op.packet = _ogg_calloc (packet_size, sizeof(unsigned char));
    if (op.packet == NULL) return;

    memset (op.packet, 0, packet_size);
    memcpy (op.packet, FISHEAD_IDENTIFIER, 8); /* identifier */
    write16le(op.packet+8, ver_maj); /* version major */
    write16le(op.packet+10, ver_min); /* version minor */
    write64le(op.packet+12, (ogg_int64_t)0); /* presentationtime numerator */
    write64le(op.packet+20, (ogg_int64_t)1000); /* presentationtime denominator */
    write64le(op.packet+28, (ogg_int64_t)0); /* basetime numerator */
    write64le(op.packet+36, (ogg_int64_t)1000); /* basetime denominator */
    /* both the numerator are zero hence handled by the memset */
    write32le(op.packet+44, 0); /* UTC time, set to zero for now */

    /* Index start/end time, if unknown or non-indexed, will be -1. */
    if (version == SKELETON_VERSION(4,0)) {
        write64le(op.packet+64, output_file_length(info));
        write64le(op.packet+72, info->content_offset);
    }
    op.b_o_s = 1; /* its the first packet of the stream */
    op.e_o_s = 0; /* its not the last packet of the stream */
    op.bytes = packet_size; /* length of the packet in bytes */

    ogg_stream_packetin (&info->so, &op); /* adding the packet to the skeleton stream */
    _ogg_free (op.packet);
}

const char* theora_message_headers = "Content-Type: video/theora\r\n"
                                     "Role: video/main\r\n"
                                     "Name: video_1\r\n";

const char* vorbis_message_headers = "Content-Type: audio/vorbis\r\n"
                                     "Role: audio/main\r\n"
                                     "Name: audio_1\r\n";
#ifdef HAVE_KATE
const char* kate_message_headers =   "Content-Type: application/x-kate\r\n\r\n"
                                     "Role: text/subtitle\r\n";
                                     /* Dynamically add name header... */
#endif

/*
 * Adds the fishead packets in the skeleton output stream along with the e_o_s packet
 */
void add_fisbone_packet (oggmux_info *info) {
    ogg_packet op;
    size_t packet_size = 0;
    if (!info->audio_only) {
        memset (&op, 0, sizeof (op));
        packet_size = FISBONE_SIZE + strlen(theora_message_headers);
        op.packet = _ogg_calloc (packet_size, sizeof(unsigned char));
        if (op.packet == NULL) return;

        memset (op.packet, 0, packet_size);
        /* it will be the fisbone packet for the theora video */
        memcpy (op.packet, FISBONE_IDENTIFIER, 8); /* identifier */
        write32le(op.packet+8, FISBONE_MESSAGE_HEADER_OFFSET); /* offset of the message header fields */
        write32le(op.packet+12, info->to.serialno); /* serialno of the theora stream */
        write32le(op.packet+16, 3); /* number of header packets */
        /* granulerate, temporal resolution of the bitstream in samples/microsecond */
        write64le(op.packet+20, info->ti.fps_numerator); /* granulrate numerator */
        write64le(op.packet+28, info->ti.fps_denominator); /* granulrate denominator */
        write64le(op.packet+36, 0); /* start granule */
        write32le(op.packet+44, 0); /* preroll, for theora its 0 */
        *(op.packet+48) = info->ti.keyframe_granule_shift; /* granule shift */
        /* message header fields */
        memcpy(op.packet+FISBONE_SIZE, theora_message_headers, strlen(theora_message_headers));

        op.b_o_s = 0;
        op.e_o_s = 0;
        op.bytes = packet_size; /* size of the packet in bytes */

        ogg_stream_packetin (&info->so, &op);
        _ogg_free (op.packet);
    }

    if (!info->video_only) {
        memset (&op, 0, sizeof (op));
        packet_size = FISBONE_SIZE + strlen(vorbis_message_headers);
        op.packet = _ogg_calloc (packet_size, sizeof(unsigned char));
        if (op.packet == NULL) return;

        memset (op.packet, 0, packet_size);
        /* it will be the fisbone packet for the vorbis audio */
        memcpy (op.packet, FISBONE_IDENTIFIER, 8); /* identifier */
        write32le(op.packet+8, FISBONE_MESSAGE_HEADER_OFFSET); /* offset of the message header fields */
        write32le(op.packet+12, info->vo.serialno); /* serialno of the vorbis stream */
        write32le(op.packet+16, 3); /* number of header packet */
        /* granulerate, temporal resolution of the bitstream in Hz */
        write64le(op.packet+20, info->sample_rate); /* granulerate numerator */
        write64le(op.packet+28, (ogg_int64_t)1); /* granulerate denominator */
        write64le(op.packet+36, 0); /* start granule */
        write32le(op.packet+44, 2); /* preroll, for vorbis its 2 */
        *(op.packet+48) = 0; /* granule shift, always 0 for vorbis */
        memcpy(op.packet+FISBONE_SIZE, vorbis_message_headers, strlen(vorbis_message_headers));

        /* Important: Check the case of Content-Type for correctness */

        op.b_o_s = 0;
        op.e_o_s = 0;
        op.bytes = packet_size;

        ogg_stream_packetin (&info->so, &op);
        _ogg_free (op.packet);
    }

#ifdef HAVE_KATE
    if (info->with_kate) {
        int n;
        char name[32];
        for (n=0; n<info->n_kate_streams; ++n) {
            size_t packet_size = 0;
            int message_headers_len = strlen(kate_message_headers);
            int name_len = 0;
            oggmux_kate_stream *ks=info->kate_streams+n;
            memset (&op, 0, sizeof (op));
            sprintf(name, "Name: %d\r\n", (n+1));
            name_len = strlen(name);
            packet_size = FISBONE_SIZE + message_headers_len + name_len;
            op.packet = _ogg_calloc (packet_size, sizeof(unsigned char));
            memset (op.packet, 0, packet_size);
            /* it will be the fisbone packet for the kate stream */
            memcpy (op.packet, FISBONE_IDENTIFIER, 8); /* identifier */
            write32le(op.packet+8, FISBONE_MESSAGE_HEADER_OFFSET); /* offset of the message header fields */
            write32le(op.packet+12, ks->ko.serialno); /* serialno of the vorbis stream */
            write32le(op.packet+16, ks->ki.num_headers); /* number of header packet */
            /* granulerate, temporal resolution of the bitstream in Hz */
            write64le(op.packet+20, ks->ki.gps_numerator); /* granulerate numerator */
            write64le(op.packet+28, ks->ki.gps_denominator); /* granulerate denominator */
            write64le(op.packet+36, 0); /* start granule */
            write32le(op.packet+44, 0); /* preroll, for kate it's 0 */
            *(op.packet+48) = ks->ki.granule_shift; /* granule shift */
            memcpy (op.packet+FISBONE_SIZE, kate_message_headers, message_headers_len);
            memcpy (op.packet+FISBONE_SIZE+message_headers_len, name, name_len);
            /* Important: Check the case of Content-Type for correctness */

            op.b_o_s = 0;
            op.e_o_s = 0;
            op.bytes = packet_size;

            ogg_stream_packetin (&info->so, &op);
            _ogg_free (op.packet);
        }
    }
#endif
}

static int keypoints_per_index(seek_index* index, double duration)
{
    double keypoints_per_second = (double)index->packet_interval / 1000.0;
    return !keypoints_per_second ? 0 : ((int)ceil(duration / keypoints_per_second) + 2);
}

/* Creates a new index packet, with |bytes| space set aside for index. */ 
static int create_index_packet(size_t bytes,
                               ogg_packet* op,
                               ogg_uint32_t serialno,
                               ogg_int64_t num_used_keypoints)
{
    size_t size = 42 + bytes;
    memset (op, 0, sizeof(*op));
    op->packet = malloc(size);
    if (op->packet == NULL)
        return -1;
    memset(op->packet, -1, size);
    op->b_o_s = 0;
    op->e_o_s = 0;
    op->bytes = size;

    /* Write identifier bytes into packet. */
    memcpy(op->packet, "index", 6);

    /* Write the serialno into packet. */
    write32le(op->packet+6, serialno);

    /* Write number of valid keypoints in index into packet. */
    write64le(op->packet+10, num_used_keypoints);

    /* Write timestamp denominator, times are in milliseconds, so 1000. */
    write64le(op->packet+18, (ogg_int64_t)1000);

    /* Write first sample time numerator. */
    write64le(op->packet+26, (ogg_int64_t)0);

    /* Write last sample time numerator. */
    write64le(op->packet+34, (ogg_int64_t)0);

    return 0;
}

/*
 * Writes a placeholder index page for a particular stream.
 */
static int write_index_placeholder_for_stream (oggmux_info *info,
                                               seek_index* index,
                                               ogg_uint32_t serialno)
{
    ogg_packet op;
    ogg_page og;
    int num_keypoints = keypoints_per_index(index,
                                            info->duration);
    if (index->packet_size == -1) {
        index->packet_size = (int)(num_keypoints * 5.1);
    }
    if (create_index_packet(index->packet_size,
                            &op,
                            serialno,
                            0) == -1)
    {
        return -1;
    }

    seek_index_set_max_keypoints(index, num_keypoints);

    /* Remember where we wrote the index pages, so that we can overwrite them
       once we've encoded the entire file. */
    index->page_location = ftello(info->outfile);

    /* There should be no packets in the stream. */
    assert(ogg_stream_flush(&info->so, &og) == 0);

    ogg_stream_packetin(&info->so, &op);
    free(op.packet);

    while (ogg_stream_flush(&info->so, &og)) {
        assert(index->page_location != 0);
        write_page (info, &og);
    }

    return 0;
}

/* Counts number of bytes required to encode n with variable byte encoding. */
static int bytes_required(ogg_int64_t n) {
    int bits = 0;
    int bytes = 0;
    assert(n >= 0);
    /* Determine number of bits required. */
    while (n) {
        n = n >> 1;
        bits++;
    }
    /* 7 bits per byte, plus 1 if we spill over onto the next byte. */
    bytes = bits / 7;
    return bytes + (((bits % 7) != 0 || bits == 0) ? 1 : 0);
}

static unsigned char*
write_vl_int(unsigned char* p, const unsigned char* limit, ogg_int64_t n)
{
    ogg_int64_t k = n;
    unsigned char* x = p;
    assert(n >= 0);
    do {
        if (p >= limit) {
            return p;
        }
        unsigned char b = (unsigned char)(k & 0x7f);
        k >>= 7;
        if (k == 0) {
            // Last byte, add terminating bit.
            b |= 0x80;
        }
        *p = b;
        p++;
    } while (k && p < limit);
    assert(x + bytes_required(n) == p);

    return p;
}

typedef struct {
    ogg_int64_t offset;
    ogg_int64_t time;
} keypoint;

/* Overwrites pages on disk for a stream's index with actual index data. */
static int
write_index_pages (seek_index* index,
                   const char* name,
                   oggmux_info *info,
                   ogg_uint32_t serialno,
                   int target_packet,
                   int num_headers)
{
    ogg_packet op;
    ogg_page og;
    int i;
    int k = 0;
    int result;
    int num_keypoints;
    int packetno;
    int last_packetno = num_headers-1; /* Take into account header packets... */
    keypoint* keypoints = 0;
    int pageno = 0;
    int prev_keyframe_pageno = -INT_MAX;
    ogg_int64_t prev_keyframe_start_time = -INT_MAX;
    int packet_in_page = 0;
    unsigned char* p = 0;
    ogg_int64_t prev_offset = 0;
    ogg_int64_t prev_time = 0;
    const unsigned char* limit = 0;
    int index_bytes = 0;  
    int keypoints_cutoff = 0;

    /* Must have indexed keypoints to go on */
    if (index->max_keypoints == 0 || index->packet_num == 0) {
      fprintf(stderr, "WARNING: no key points for %s stream %08x\n", name, serialno);
      return 0;
    }

    /* Must have placeholder packet to rewrite. */
    assert(index->page_location > 0);

    num_keypoints = index->max_keypoints;

    /* Calculate and store the keypoints. */
    keypoints = (keypoint*)malloc(sizeof(keypoint) * num_keypoints);
    if (!keypoints) {
        fprintf(stderr, "ERROR: malloc failure in rewrite_index_page\n");
        return -1;
    }
    memset(keypoints, -1, sizeof(keypoint) * num_keypoints);

    prev_offset = 0;
    prev_time = 0;
    for (i=0; i < index->packet_num && k < num_keypoints; i++) {
        packetno = index->packets[i].packetno;
        /* Increment pageno until we find the page which contains the start of
         * the keyframe's packet. */
        while (pageno < index->pages_num &&
               last_packetno + index->pages[pageno].packet_start_num < packetno)
        {
            last_packetno += index->pages[pageno].packet_start_num;
            pageno++;
        }
        assert(pageno < index->pages_num);
    
        if (prev_keyframe_pageno != pageno) {
            /* First keyframe/sample in this page. */
            packet_in_page = 1;
            prev_keyframe_pageno = pageno;
        } else {
            packet_in_page++;
        }

        if (packet_in_page != target_packet ||
            index->packets[i].start_time <= (prev_keyframe_start_time + index->packet_interval)) {
            /* Either this isn't the keyframe we want to index on this page, or
               the keyframe occurs too close to the previously indexed one, so
               skip to the next one. */
            continue;
        }

        /* Add to final keyframe index. */        
        keypoints[k].offset = index->pages[pageno].offset;
        keypoints[k].time = index->packets[i].start_time;
        
        /* Count how many bytes is required to encode this keypoint. */
        index_bytes += bytes_required(keypoints[k].offset - prev_offset);
        prev_offset = keypoints[k].offset;
        index_bytes += bytes_required(keypoints[k].time - prev_time);
        prev_time = keypoints[k].time;

        k++;

        if (index_bytes < index->packet_size) {
            keypoints_cutoff = k;
        }

        prev_keyframe_start_time = index->packets[i].start_time;
    }
    if (index_bytes > index->packet_size) {
        printf("WARNING: Underestimated space for %s keyframe index, dropped %d keyframes, "
               "only part of the file may be indexed. Rerun with --%s-index-reserve %d to "
               "ensure a complete index, or use OggIndex to re-index.\n",
               name, (k - keypoints_cutoff), name, index_bytes);
    } else if (index_bytes < index->packet_size &&
               index->packet_size - index_bytes > 10000)
    {
        /* We over estimated the index size by 10,000 bytes or more. */
        printf("Allocated %d bytes for %s keyframe index, %d are unused. "
               "Index contains %d keyframes. "
               "Rerun with '--%s-index-reserve %d' to encode with the optimal sized %s index,"
               " or use OggIndex to re-index.\n",
               index->packet_size, name, (index->packet_size - index_bytes),
               keypoints_cutoff,
               name, index_bytes, name);
    }
    num_keypoints = keypoints_cutoff;

    if (create_index_packet(index->packet_size,
                            &op,
                            serialno,
                            num_keypoints) == -1)
    {
        free(keypoints);
        return -1;
    }

    /* Write first sample time numerator. */
    write64le(op.packet+26, index->start_time);

    /* Write last sample time numerator. */
    write64le(op.packet+34, index->end_time);
   
    /* Write keypoint data into packet. */
    p = op.packet + 42;
    limit = op.packet + op.bytes;
    prev_offset = 0;
    prev_time = 0;
    for (i=0; i<num_keypoints; i++) {
        keypoint* k = &keypoints[i];
        ogg_int64_t offset_diff = k->offset - prev_offset;
        ogg_int64_t time_diff = k->time - prev_time;
        p = write_vl_int(p, limit, offset_diff);
        p = write_vl_int(p, limit, time_diff);
        prev_offset = k->offset;
        prev_time = k->time;
    }
    free(keypoints);

    /* Skeleton stream must be empty. */
    assert(ogg_stream_flush(&info->so, &og) == 0);
    ogg_stream_packetin(&info->so, &op);
    free(op.packet);

    /* Seek to location of existing index pages. */
    if (fseeko(info->outfile, index->page_location, SEEK_SET) < 0) {
        fprintf(stderr, "ERROR: Can't seek output file to write index.!\n");
        return -1;
    }

    /* Overwrite pages to disk. */
    while (ogg_stream_pageout(&info->so, &og)) {
        /* Index pages should not be BOS or EOS. */
        assert(!(og.header[5] & 0x6));
        write_page (info, &og);
    }

    /* Ensure we're flushed to disk. */
    result = ogg_stream_flush(&info->so, &og);
    if (result != 0) {
        write_page (info, &og);
    }

    return 0;
}

/* Overwrites existing skeleton index placeholder packets with valid keyframe
   index data. Must only be called once we've constructed the index data after
   encoding the entire file. */
int write_seek_index (oggmux_info* info)
{
    ogg_uint32_t serialno;
    ogg_page og;

    /* We shouldn't write indexes for skeleton 3, it's a skeleton 4 feature. */
    assert(!info->skeleton_3);

    /* Mark that we're done indexing. This causes the header packets' fields
       to be filled with valid, non-unknown values. */
    info->indexing_complete = 1;

    /* Re-encode the entire skeleton track, to ensure the packet and page
       counts don't change. */
    serialno = info->so.serialno;
    ogg_stream_clear(&info->so);
    ogg_stream_init(&info->so, serialno);

    add_fishead_packet (info, 4, 0);
    if (ogg_stream_flush(&info->so, &og) != 1) {
        fprintf (stderr, "Internal Ogg library error.\n");
        exit (1);
    }

    /* Rewrite the skeleton BOS page. It will have changed to account for
       learning the start time, end time, and length. */
    if (fseeko(info->outfile, 0, SEEK_SET) < 0) {
        fprintf(stderr, "ERROR: Can't seek output file to rewrite skeleton BOS!\n");
        return -1;
    }
    write_page (info, &og);

    /* Encode and discard fisbone packets, their contents' doesn't need
       to be updated. */
    add_fisbone_packet(info);
    while (1) {
        int result = ogg_stream_flush(&info->so, &og);
        if (result < 0) {
            fprintf(stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        if (result == 0)
            break;
    }

    /* Write a new line, so that when we print out indexing stats, it's on a new line. */
    printf("\n");
    if (!info->audio_only &&
        write_index_pages(&info->theora_index,
                          "theora",
                          info,
                          info->to.serialno,
                          1,
                          3) == -1)
    {
        return -1;
    }
    if (!info->video_only && 
        write_index_pages(&info->vorbis_index,
                          "vorbis",
                          info,
                          info->vo.serialno,
                          2,
                          3) == -1)
    {
        return -1;
    }

#ifdef HAVE_KATE
    if (info->with_kate) {
        int n;
        for (n=0; n<info->n_kate_streams; ++n) {
            oggmux_kate_stream *ks=info->kate_streams+n;
            if (write_index_pages(&ks->index, "kate", info, ks->ko.serialno, 1, ks->ki.num_headers) == -1)
            {
                return -1;
            }
        }
    }
#endif

    return 0;
}

/* Adds skeleton index packets to the output file at the current write cursor
   for every stream. We'll fill them with valid data later, we just fill them
   with placeholder data so that we don't need to rewrite the entire file
   after encode when we add the index. */
static int write_placeholder_index_pages (oggmux_info *info)
{
    if (info->theora_index_reserve != -1) {
        info->theora_index.packet_size = info->theora_index_reserve;
    }
    if (!info->audio_only &&
        write_index_placeholder_for_stream(info,
                                           &info->theora_index,
                                           info->to.serialno) == -1)
    {
        return -1;
    }
    if (info->vorbis_index_reserve != -1) {
        info->vorbis_index.packet_size = info->vorbis_index_reserve;
    }
    if (!info->video_only &&
        write_index_placeholder_for_stream(info,
                                           &info->vorbis_index,
                                           info->vo.serialno) == -1)
    {
        return -1;
    }

#ifdef HAVE_KATE
    if (info->with_kate) {
        int n;
        for (n=0; n<info->n_kate_streams; ++n) {
            oggmux_kate_stream *ks=info->kate_streams+n;
            if (info->kate_index_reserve != -1) {
                ks->index.packet_size = info->kate_index_reserve;
            }
            if (write_index_placeholder_for_stream(info,
                                                   &ks->index,
                                                   ks->ko.serialno) == -1)
            {
                return -1;
            }
        }
    }
#endif

    return 0;
}

void oggmux_init (oggmux_info *info) {
    ogg_page og;
    ogg_packet op;
    int ret;

    /* yayness.  Set up Ogg output stream */
    srand (time (NULL));
    info->serialno = rand();
    ogg_stream_init (&info->vo, info->serialno++);

    if (info->passno!=1) {
        th_comment_add_tag(&info->tc, "ENCODER", PACKAGE_STRING);
        vorbis_comment_add_tag(&info->vc, "ENCODER", PACKAGE_STRING);
        if (strcmp(info->oshash, "0000000000000000") > 0) {
            th_comment_add_tag(&info->tc, "SOURCE_OSHASH", info->oshash);
            vorbis_comment_add_tag(&info->vc, "SOURCE_OSHASH", info->oshash);
        }
    }

    if (!info->audio_only) {
        ogg_stream_init (&info->to, info->serialno++);
        seek_index_init(&info->theora_index, info->index_interval);
    }
    /* init theora done */
    /* initialize Vorbis too, if we have audio. */
    if (!info->video_only) {
        int ret;
        vorbis_info_init (&info->vi);
        /* Encoding using a VBR quality mode.  */
        if (info->vorbis_quality>-99)
            ret =vorbis_encode_init_vbr (&info->vi, info->channels,info->sample_rate,info->vorbis_quality);
        else
            ret=vorbis_encode_init(&info->vi,info->channels,info->sample_rate,-1,info->vorbis_bitrate,-1);

        if (ret) {
            fprintf (stderr,
                 "The Vorbis encoder could not set up a mode according to\n"
                 "the requested quality or bitrate.\n\n");
            exit (1);
        }

        /* set up the analysis state and auxiliary encoding storage */
        vorbis_analysis_init (&info->vd, &info->vi);
        vorbis_block_init (&info->vd, &info->vb);
        
        seek_index_init(&info->vorbis_index, info->index_interval);
        info->vorbis_granulepos = 0;
    }
    /* audio init done */

    /* initialize kate if we have subtitles */
    if (info->with_kate && info->passno!=1) {
#ifdef HAVE_KATE
        int ret, n;
        for (n=0; n<info->n_kate_streams; ++n) {
            oggmux_kate_stream *ks=info->kate_streams+n;
            ogg_stream_init (&ks->ko, info->serialno++);
            ret = kate_encode_init (&ks->k, &ks->ki);
            if (ret<0) {
                fprintf(stderr, "kate_encode_init: %d\n",ret);
                exit(1);
            }
            ret = kate_comment_init(&ks->kc);
            if (ret<0) {
                fprintf(stderr, "kate_comment_init: %d\n",ret);
                exit(1);
            }
            kate_comment_add_tag (&ks->kc, "ENCODER",PACKAGE_STRING);

            seek_index_init(&ks->index, info->index_interval);
        }
#endif
    }
    /* kate init done */

    if (info->with_skeleton &&
        !info->skeleton_3 &&
        info->duration == -1)
    {
        /* We've not got a duration, we can't index the keyframes. */
        fprintf(stderr, "WARNING: Can't get duration of media, not indexing, writing Skeleton 3 track.\n");
        info->skeleton_3 = 1;
    }

    /* first packet should be skeleton fishead packet, if skeleton is used */

    if (info->with_skeleton && info->passno!=1) {
        /* Sometimes the output file is not seekable. We can't write the seek
           index if the output is not seekable. So write a Skeleton3.0 header
           packet, which will in turn determine if the file is seekable. If it
           is, we can safely construct an index, so then overwrite the header
           page with a Skeleton4.0 header page. */
        int skeleton_3 = info->skeleton_3;
        info->skeleton_3 = 1;
        ogg_stream_init (&info->so, info->serialno++);
        add_fishead_packet (info, 3, 0);
        if (ogg_stream_pageout (&info->so, &og) != 1) {
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        write_page (info, &og);
        assert(info->output_seekable != MAYBE_SEEKABLE);

        if (info->output_seekable == NOT_SEEKABLE && !skeleton_3) {
            fprintf(stderr, "WARNING: Can't write keyframe-seek-index into "
                            "non-seekable output stream! Writing Skeleton3 track.\n");
        }

        info->skeleton_3 = skeleton_3 || info->output_seekable == NOT_SEEKABLE;

        if (!info->skeleton_3) {
            /* Output is seekable and we're indexing. Overwrite the
               Skeleton3.0 BOS page with a Skeleton4.0 BOS page. */
            if (fseeko (info->outfile, 0, SEEK_SET) < 0) {
                fprintf (stderr, "ERROR: failed to seek in seekable output file!?!\n");
                exit (1);
            }
            ogg_stream_clear (&info->so);
            ogg_stream_init (&info->so, info->serialno++);
            add_fishead_packet (info, 4, 0);
            if (ogg_stream_pageout (&info->so, &og) != 1) {
                fprintf (stderr, "Internal Ogg library error.\n");
                exit (1);
            }
            write_page (info, &og);
        }
    }

    /* write the bitstream header packets with proper page interleave */

    /* first packet will get its own page automatically */
    if (!info->audio_only) {
        /* write the bitstream header packets with proper page interleave */
        /* first packet will get its own page automatically */
        if(th_encode_flushheader(info->td, &info->tc, &op) <= 0) {
          fprintf(stderr, "Internal Theora library error.\n");
          exit(1);
        }
        if(info->passno!=1){
            ogg_stream_packetin(&info->to, &op);
            if(ogg_stream_pageout(&info->to, &og) != 1) {
                fprintf(stderr, "Internal Ogg library error.\n");
                exit(1);
            }
            write_page (info, &og);
        }

        /* create the remaining theora headers */
        for(;;){
          ret=th_encode_flushheader(info->td, &info->tc, &op);
          if(ret < 0) {
            fprintf(stderr,"Internal Theora library error.\n");
            exit(1);
          }
          else if(!ret) break;
          if(info->passno!=1)
            ogg_stream_packetin(&info->to, &op);
        }
    }
    if (!info->video_only && info->passno!=1) {
        ogg_packet header;
        ogg_packet header_comm;
        ogg_packet header_code;

        vorbis_analysis_headerout (&info->vd, &info->vc, &header,
                       &header_comm, &header_code);
        ogg_stream_packetin (&info->vo, &header);    /* automatically placed in its own
                                 * page */
        if (ogg_stream_pageout (&info->vo, &og) != 1) {
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        write_page (info, &og);

        /* remaining vorbis header packets */
        ogg_stream_packetin (&info->vo, &header_comm);
        ogg_stream_packetin (&info->vo, &header_code);
    }

#ifdef HAVE_KATE
    if (info->with_kate && info->passno!=1) {
        int n;
        for (n=0; n<info->n_kate_streams; ++n) {
            oggmux_kate_stream *ks=info->kate_streams+n;
            int ret;
            while (1) {
                ret=kate_ogg_encode_headers(&ks->k,&ks->kc,&op);
                if (ret==0) {
                    ogg_stream_packetin(&ks->ko,&op);
                    ogg_packet_clear(&op);
                }
                if (ret<0) fprintf(stderr, "kate_encode_headers: %d\n",ret);
                if (ret>0) break;
            }

            /* first header is on a separate page - libogg will do it automatically */
            ret=ogg_stream_pageout (&ks->ko, &og);
            if (ret!=1) {
                fprintf (stderr, "Internal Ogg library error.\n");
                exit (1);
            }
            write_page (info, &og);
        }
    }
#endif

    /* output the appropriate fisbone packets */
    if (info->with_skeleton && info->passno!=1) {
        add_fisbone_packet (info);
        while (1) {
            int result = ogg_stream_flush (&info->so, &og);
                if (result < 0) {
                /* can't get here */
                fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
                }
            if (result == 0)
                break;
            write_page (info, &og);
        }
    }

    /* Flush the rest of our headers. This ensures
     * the actual data in each stream will start
     * on a new page, as per spec. */
    while (1 && !info->audio_only && info->passno!=1) {
        int result = ogg_stream_flush (&info->to, &og);
        if (result < 0) {
            /* can't get here */
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        if (result == 0)
            break;
        write_page (info, &og);
    }
    while (1 && !info->video_only && info->passno!=1) {
        int result = ogg_stream_flush (&info->vo, &og);
        if (result < 0) {
            /* can't get here */
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        if (result == 0)
            break;
        write_page (info, &og);
    }
#ifdef HAVE_KATE
    if (info->with_kate && info->passno!=1) {
        int n;
        for (n=0; n<info->n_kate_streams; ++n) {
            oggmux_kate_stream *ks=info->kate_streams+n;
            while (1) {
                int result = ogg_stream_flush (&ks->ko, &og);
                if (result < 0) {
                    /* can't get here */
                    fprintf (stderr, "Internal Ogg library error.\n");
                    exit (1);
                }
                if (result == 0)
                    break;
                write_page (info, &og);
            }
        }
    }
#endif

    if (info->with_skeleton && info->passno!=1) {
        int result;

        if (!info->skeleton_3) {
            /* Add placeholder packets to reserve space for the index
             * at the start of file. */
            write_placeholder_index_pages (info);
        }

        /* build and add the e_o_s packet */
        memset (&op, 0, sizeof (op));
            op.b_o_s = 0;
        op.e_o_s = 1; /* its the e_o_s packet */
            op.granulepos = 0;
        op.bytes = 0; /* e_o_s packet is an empty packet */
            ogg_stream_packetin (&info->so, &op);

        result = ogg_stream_flush (&info->so, &og);
        if (result < 0) {
            /* can't get here */
            fprintf (stderr, "Internal Ogg library error.\n");
            exit (1);
        }
        write_page (info, &og);
        
        /* Record the offset of the next page; it's the first non-header, or
         * content page. */
        info->content_offset = ftello(info->outfile);
    }
}

/**
 * adds a video frame to the encoding sink
 * if e_o_s is 1 the end of the logical bitstream will be marked.
 * @param this ff2theora struct
 * @param info oggmux_info
 * @param yuv_buffer
 * @param e_o_s 1 indicates ond of stream
 */
void oggmux_add_video (oggmux_info *info, th_ycbcr_buffer ycbcr, int e_o_s) {
    ogg_packet op;
    int ret;

    if(info->passno==2){
        for(;;){
          static unsigned char buffer[80];
          static int buf_pos;
          int bytes;
          /*Ask the encoder how many bytes it would like.*/
          bytes=th_encode_ctl(info->td,TH_ENCCTL_2PASS_IN,NULL,0);
          if(bytes<0){
            fprintf(stderr,"Error submitting pass data in second pass.\n");
            exit(1);
          }
          /*If it's got enough, stop.*/
          if(bytes==0)break;
          /*Read in some more bytes, if necessary.*/
          if(bytes>80-buf_pos)bytes=80-buf_pos;
          if(bytes>0&&fread(buffer+buf_pos,1,bytes,info->twopass_file)<bytes){
            fprintf(stderr,"Could not read frame data from two-pass data file!\n");
            exit(1);
          }
          /*And pass them off.*/
          ret=th_encode_ctl(info->td,TH_ENCCTL_2PASS_IN,buffer,bytes);
          if(ret<0){
            fprintf(stderr,"Error submitting pass data in second pass.\n");
            exit(1);
          }
          /*If the encoder consumed the whole buffer, reset it.*/
          if(ret>=bytes)buf_pos=0;
          /*Otherwise remember how much it used.*/
          else buf_pos+=ret;
        }
    }

    th_encode_ycbcr_in(info->td, ycbcr);
    /* in two-pass mode's first pass we need to extract and save the pass data */
    if(info->passno==1){

        unsigned char *buffer;
        int bytes = th_encode_ctl(info->td, TH_ENCCTL_2PASS_OUT, &buffer, sizeof(buffer));
        if(bytes<0){
          fprintf(stderr,"Could not read two-pass data from encoder.\n");
          exit(1);
        }
        if(fwrite(buffer,1,bytes,info->twopass_file)<bytes){
          fprintf(stderr,"Unable to write to two-pass data file.\n");
          exit(1);
        }
        fflush(info->twopass_file);
    }

    while (th_encode_packetout (info->td, e_o_s, &op) > 0) {
        if (!info->skeleton_3 &&
            info->passno != 1)
        {
            ogg_int64_t frameno = th_granule_frame(info->td, op.granulepos);
            ogg_int64_t start_time = (1000 * info->ti.fps_denominator * frameno) /
                                     info->ti.fps_numerator;
            ogg_int64_t end_time =   (1000 * info->ti.fps_denominator * (frameno + 1)) /
                                     info->ti.fps_numerator;
            seek_index_record_sample(&info->theora_index,
                                     op.packetno,
                                     start_time,
                                     end_time,
                                     th_packet_iskeyframe(&op));
        }
        ogg_stream_packetin (&info->to, &op);
        info->v_pkg++;
    }
    if(info->passno==1 && e_o_s){
        /* need to read the final (summary) packet */
        unsigned char *buffer;
        int bytes = th_encode_ctl(info->td, TH_ENCCTL_2PASS_OUT, &buffer, sizeof(buffer));
        if(bytes<0){
          fprintf(stderr,"Could not read two-pass summary data from encoder.\n");
          exit(1);
        }
        if(fseek(info->twopass_file,0,SEEK_SET)<0){
          fprintf(stderr,"Unable to seek in two-pass data file.\n");
          exit(1);
        }
        if(fwrite(buffer,1,bytes,info->twopass_file)<bytes){
          fprintf(stderr,"Unable to write to two-pass data file.\n");
          exit(1);
        }
        fflush(info->twopass_file);
    }
}

static ogg_int64_t
vorbis_time(vorbis_dsp_state * dsp, ogg_int64_t granulepos) {
    return 1000 * granulepos / dsp->vi->rate;
}

/**
 * adds audio samples to encoding sink
 * @param buffer pointer to buffer
 * @param samples samples in buffer
 * @param e_o_s 1 indicates end of stream.
 */
void oggmux_add_audio (oggmux_info *info, uint8_t **buffer, int samples, int e_o_s) {
    ogg_packet op;

    int i, j, k, count = 0;
    float **vorbis_buffer;

    if (samples <= 0) {
        /* end of audio stream */
        if (e_o_s)
            vorbis_analysis_wrote (&info->vd, 0);
    }
    else{
        vorbis_buffer = vorbis_analysis_buffer (&info->vd, samples);
        /* uninterleave samples */
        for (i = 0; i < samples; i++) {
            for (j=0;j<info->channels;j++) {
                k = j;
                /* 5.1 input: [fl, fr, c, lfe, rl, rr] */
                if(info->channels == 6) {
                    switch(j) {
                        case 0: k = 0; break;
                        case 1: k = 2; break;
                        case 2: k = 1; break;
                        case 3: k = 5; break;
                        case 4: k = 3; break;
                        case 5: k = 4; break;
                        default: k = j;
                    }
                }
                vorbis_buffer[k][i] = ((const float  *)buffer[j])[i];
            }
        }
        vorbis_analysis_wrote (&info->vd, samples);
        /* end of audio stream */
        if (e_o_s)
            vorbis_analysis_wrote (&info->vd, 0);
    }

    while (vorbis_analysis_blockout (&info->vd, &info->vb) == 1) {
        /* analysis, assume we want to use bitrate management */
        vorbis_analysis (&info->vb, NULL);
        vorbis_bitrate_addblock (&info->vb);

        /* weld packets into the bitstream */
        if (vorbis_bitrate_flushpacket (&info->vd, &op)) {
            assert(op.granulepos != -1);
            
            /* For indexing, we must accurately know the presentation time of
               the first sample we can decode on any page. Vorbis packets
               require data from their preceeding packet to decode. To
               calculate the number of samples in this block, we need to take
               into account the number of samples in the previous block. Once
               we accurately know the samples in each packet, the presentation
               time of a vorbis page is the presentation time of the second
               packet in the page. */
            int num_samples = (info->prev_vorbis_window == -1) ? 0 :
                               info->prev_vorbis_window/4 + info->vb.pcmend / 4;
            info->prev_vorbis_window = info->vb.pcmend;

            ogg_int64_t start_granule = op.granulepos - num_samples;
            if (start_granule < 0) {
                /* The first vorbis content packet can have more samples than
                   its granulepos reports. This is allowed by the spec, and
                   players should discard the leading samples and not play them.
                   Thus the indexer needs to discard them as well.*/
                if (op.packetno != 4) {
                    /* We only expect negative start granule in the first content
                       packet, not any of the others... */
                    fprintf(stderr, "WARNING: vorbis packet %" PRId64 " has calculated start"
                            " granule of %" PRId64 ", but it should be non-negative!",
                            op.packetno, start_granule);
                }
                start_granule = 0;
            }
            if (start_granule < info->vorbis_granulepos) {
                /* This packet starts before the end of the previous packet. This is
                   allowed by the specification in the last packet only, and the
                   trailing samples should be discarded and not played/indexed. */
                if (!op.e_o_s) {
                    fprintf(stderr, "WARNING: vorbis packet %" PRId64 " (granulepos %" PRId64 ") starts before"
                            " the end of the preceeding packet!", op.packetno, op.granulepos);
                }
                start_granule = info->vorbis_granulepos;
            }
            info->vorbis_granulepos = op.granulepos;
            ogg_int64_t start_time = vorbis_time (&info->vd, start_granule);
            
            if (op.granulepos != -1 &&
                !info->skeleton_3 &&
                info->passno != 1)
            {
                ogg_int64_t end_time = vorbis_time (&info->vd, op.granulepos);
                seek_index_record_sample(&info->vorbis_index,
                                         op.packetno,
                                         start_time,
                                         end_time,
                                         1);
            }
            ogg_stream_packetin (&info->vo, &op);
            info->a_pkg++;
        }
        /* libvorbis should encode with 1:1 block:packet ratio. If not, our
           vorbis sample length calculations will be wrong! */
        assert(vorbis_bitrate_flushpacket (&info->vd, &op) == 0);
    }

}

static void oggmux_record_kate_index(oggmux_info *info, oggmux_kate_stream *ks, const ogg_packet *op, ogg_int64_t start_time, ogg_int64_t end_time)
{
    if (ks->last_end_time >= 0)
        start_time = ks->last_end_time;

    seek_index_record_sample(&ks->index, op->packetno, start_time, end_time, 1);
    ks->last_end_time = end_time;
}

#ifdef HAVE_KATE

/**
 * adds a subtitles text to the encoding sink
 * if e_o_s is 1 the end of the logical bitstream will be marked.
 * @param info oggmux_info
 * @param idx which kate stream to output to
 * @param t0 the show time of the text
 * @param t1 the hide time of the text
 * @param text the utf-8 text
 * @param len the number of bytes in the text
 */
void oggmux_add_kate_text (oggmux_info *info, int idx, double t0, double t1, const char *text, size_t len, int x1, int x2, int y1, int y2) {
    ogg_packet op;
    oggmux_kate_stream *ks=info->kate_streams+idx;
    int ret;
    if (x1!=-INT_MAX && y1!=-INT_MAX && x2!=-INT_MAX && y2!=-INT_MAX) {
      kate_region kr;
      kate_region_init(&kr);
      kr.metric=kate_pixel;
      kr.x=x1;
      kr.y=y1;
      kr.w=x2-x1+1;
      kr.h=y2-y1+1;
      ret=kate_encode_set_region(&ks->k,&kr);
      if (ret<0) {
        fprintf(stderr,"Error setting region: %d\n",ret);
        return;
      }
    }
    ret = kate_ogg_encode_text(&ks->k, t0, t1, text, len, &op);
    if (ret>=0) {
        if (!info->skeleton_3 && info->passno != 1) {
            ogg_int64_t start_time = (int)(t0 * 1000.0f + 0.5f);
            ogg_int64_t end_time = (int)(t1 * 1000.0f + 0.5f);
            oggmux_record_kate_index(info, ks, &op, start_time, end_time);
        }

        ogg_stream_packetin (&ks->ko, &op);
        ogg_packet_clear (&op);
        info->k_pkg++;
    }
    else {
        fprintf(stderr, "Failed to encode kate data packet (%f --> %f, [%s]): %d\n",
            t0, t1, text, ret);
    }
}

/**
 * adds a subtitles image to the encoding sink
 * if e_o_s is 1 the end of the logical bitstream will be marked.
 * @param info oggmux_info
 * @param idx which kate stream to output to
 * @param t0 the show time of the text
 * @param t1 the hide time of the text
 * @param kr the region in which to display the subtitle
 * @param kp the palette to use for the image
 * @param kb the image itself
 */
void oggmux_add_kate_image (oggmux_info *info, int idx, double t0, double t1, const kate_region *kr, const kate_palette *kp, const kate_bitmap *kb) {
    ogg_packet op;
    oggmux_kate_stream *ks=info->kate_streams+idx;
    int ret;
    ret = kate_encode_set_region(&ks->k, kr);
    if (ret >= 0) ret = kate_encode_set_palette(&ks->k, kp);
    if (ret >= 0) ret = kate_encode_set_bitmap(&ks->k, kb);
    if (ret >= 0) ret = kate_ogg_encode_text(&ks->k, t0, t1, "", 0, &op);
    if (ret>=0) {
        if (!info->skeleton_3 && info->passno != 1) {
            ogg_int64_t start_time = (int)(t0 * 1000.0f + 0.5f);
            ogg_int64_t end_time = (int)(t1 * 1000.0f + 0.5f);
            oggmux_record_kate_index(info, ks, &op, start_time, end_time);
        }

        ogg_stream_packetin (&ks->ko, &op);
        ogg_packet_clear (&op);
        info->k_pkg++;
    }
    else {
        fprintf(stderr, "Failed to encode kate data packet (%f --> %f, image): %d\n",
            t0, t1, ret);
    }
}

/**
 * adds a kate end packet to the encoding sink
 * @param info oggmux_info
 * @param idx which kate stream to output to
 * @param t the time of the end packet
 */
void oggmux_add_kate_end_packet (oggmux_info *info, int idx, double t) {
    ogg_packet op;
    oggmux_kate_stream *ks=info->kate_streams+idx;
    int ret;
    ret = kate_ogg_encode_finish(&ks->k, t, &op);
    if (ret>=0) {
        if (!info->skeleton_3 && info->passno != 1) {
            ogg_int64_t start_time = floorf(t * 1000.0f + 0.5f);
            ogg_int64_t end_time = start_time;
            oggmux_record_kate_index(info, ks, &op, start_time, end_time);
        }
        ogg_stream_packetin (&ks->ko, &op);
        ogg_packet_clear (&op);
        info->k_pkg++;
    }
    else {
        fprintf(stderr, "Failed to encode kate end packet at %f: %d\n", t, ret);
    }
}

#endif

static double get_remaining(oggmux_info *info, double timebase) {
  double remaining = 0;
  double to_encode, time_so_far;

    if (info->duration != -1 && timebase > 0) {
        time_so_far = time(NULL) - info->start_time;
        to_encode = info->duration - timebase;
        if (to_encode > 0) {
            remaining = (time_so_far / timebase) * to_encode;
        }
    }
    return remaining;
}

static double estimated_size(oggmux_info *info, double timebase) {
  double projected_size = 0;

  if (info->duration != -1 && timebase > 0) {
    projected_size = ((info->audio_bytesout + info->video_bytesout)  /  timebase) * info->duration / 1024 / 1024;
  }
  return projected_size;
}

static void print_stats(oggmux_info *info, double timebase) {
    static double last = -2;
    int hundredths = timebase * 100 - (long) timebase * 100;
    int seconds = (long) timebase % 60;
    int minutes = ((long) timebase / 60) % 60;
    int hours = (long) timebase / 3600;
    double remaining = get_remaining(info, timebase);
    int remaining_seconds = (long) remaining % 60;
    int remaining_minutes = ((long) remaining / 60) % 60;
    int remaining_hours = (long) remaining / 3600;

    if (info->passno==1) {
        if (timebase - last > 0.5 || timebase < last) {
            last = timebase;
            if (info->frontend) {
                fprintf(info->frontend, "{\"duration\": %lf, \"position\": %.02lf, \"remaining\": %.02lf}\n",
                    info->duration,
                    timebase,
                    remaining
                );
                fflush (info->frontend);
            } else {
                fprintf (stderr,"\rScanning first pass pos: %d:%02d:%02d.%02d ET: %02d:%02d:%02d             ",
                    hours, minutes, seconds, hundredths,
                    remaining_hours, remaining_minutes, remaining_seconds
                );
            }
        }

    } 
    else if (timebase - last > 0.5 || timebase < last || !remaining) {
        last = timebase;
        if (info->frontend) {
#ifdef WIN32
            fprintf(info->frontend, "{\"duration\": %f, \"position\": %.02f, \"audio_kbps\":  %d, \"video_kbps\": %d, \"remaining\": %.02f}\n",
                info->duration,
                timebase,
                info->akbps, info->vkbps,
                remaining
            );
#else
            fprintf(info->frontend, "{\"duration\": %lf, \"position\": %.02lf, \"audio_kbps\":  %d, \"video_kbps\": %d, \"remaining\": %.02lf}\n",
                info->duration,
                timebase,
                info->akbps, info->vkbps,
                remaining
            );
#endif
            fflush (info->frontend);
        }
        else if (timebase > 0) {
            if (!remaining) {
                remaining = time(NULL) - info->start_time;
                remaining_seconds = (long) remaining % 60;
                remaining_minutes = ((long) remaining / 60) % 60;
                remaining_hours = (long) remaining / 3600;
                fprintf (stderr,"\r  %d:%02d:%02d.%02d audio: %dkbps video: %dkbps, time elapsed: %02d:%02d:%02d           ",
                    hours, minutes, seconds, hundredths,
                    info->akbps, info->vkbps,
                    remaining_hours, remaining_minutes, remaining_seconds
                );
            }
            else {
                fprintf (stderr,"\r  %d:%02d:%02d.%02d audio: %dkbps video: %dkbps, ET: %02d:%02d:%02d, est. size: %.01lf MB   ",
                    hours, minutes, seconds, hundredths,
                    info->akbps, info->vkbps,
                    remaining_hours, remaining_minutes, remaining_seconds,
                    estimated_size(info, timebase)
                );
            }
        }
    }
}

/* Returns the number of packets that start on a page. */
static int
ogg_page_start_packets(unsigned char* page)
{
  int i;
  /* If we're not continuing a packet, we're at a packet start. */
  int packets_start = (page[5]&0x01) ? 0 : 1; 
  int num_lacing_vals = page[26];
  unsigned char* lacing_vals = &page[27];
  for (i=1; i<num_lacing_vals; i++) {
    if (lacing_vals[i-1] < 0xff)
      packets_start++;
  }
  return packets_start;
}


static void write_audio_page(oggmux_info *info)
{
    int ret;
    ogg_int64_t page_offset = ftello(info->outfile);
    int packets = ogg_page_packets((ogg_page *)&info->audiopage);
    int packet_start_num = ogg_page_start_packets(info->audiopage);

    ret = fwrite(info->audiopage, 1, info->audiopage_len, info->outfile);
    if (ret < info->audiopage_len) {
        fprintf(stderr,"error writing audio page\n");
    }
    else {
        info->audio_bytesout += ret;
    }
    info->audiopage_valid = 0;
    info->a_pkg -= packets;

    ret = seek_index_record_page(&info->vorbis_index,
                                 page_offset,
                                 packet_start_num);
    assert(ret == 0);
#ifdef OGGMUX_DEBUG
    info->a_page++;
    info->v_page=0;
    fprintf(stderr,"\naudio page %d (%d pkgs) | pkg remaining %d\n",info->a_page,ogg_page_packets((ogg_page *)&info->audiopage),info->a_pkg);
#endif

    info->akbps = rint (info->audio_bytesout * 8. / info->audiotime * .001);
    if (info->akbps<0)
        info->akbps=0;
    print_stats(info, info->audiotime);
}

static void write_video_page(oggmux_info *info)
{
    int ret;
    ogg_int64_t page_offset = ftello(info->outfile);
    int packets = ogg_page_packets((ogg_page *)&info->videopage);
    int packet_start_num = ogg_page_start_packets(info->videopage);

    ret = fwrite(info->videopage, 1, info->videopage_len, info->outfile);
    if (ret < info->videopage_len) {
        fprintf(stderr,"error writing video page\n");
    }
    else {
        info->video_bytesout += ret;
    }
    info->videopage_valid = 0;
    info->v_pkg -= packets;

    ret = seek_index_record_page(&info->theora_index,
                                 page_offset,
                                 packet_start_num);
    assert(ret == 0);
#ifdef OGGMUX_DEBUG
    info->v_page++;
    info->a_page=0;
    fprintf(stderr,"\nvideo page %d (%d pkgs) | pkg remaining %d\n",info->v_page,ogg_page_packets((ogg_page *)&info->videopage),info->v_pkg);
#endif

    info->vkbps = rint (info->video_bytesout * 8. / info->videotime * .001);
    if (info->vkbps<0)
        info->vkbps=0;
    print_stats(info, info->videotime);
}

static void write_kate_page(oggmux_info *info, int idx)
{
    int ret;
    oggmux_kate_stream *ks=info->kate_streams+idx;
    ogg_int64_t page_offset = ftello(info->outfile);
    int packet_start_num = ogg_page_start_packets(ks->katepage);

    ret = fwrite(ks->katepage, 1, ks->katepage_len, info->outfile);
    if (ret < ks->katepage_len) {
        fprintf(stderr,"error writing kate page\n");
    }
    else {
        info->kate_bytesout += ret;
    }
    ks->katepage_valid = 0;
    info->k_pkg -= ogg_page_packets((ogg_page *)&ks->katepage);

    ret = seek_index_record_page(&ks->index,
                                 page_offset,
                                 packet_start_num);
    assert(ret == 0);

#ifdef OGGMUX_DEBUG
    info->k_page++;
    fprintf(stderr,"\nkate page %d (%d pkgs) | pkg remaining %d\n",info->k_page,ogg_page_packets((ogg_page *)&ks->katepage),info->k_pkg);
#endif


    /*
    info->kkbps = rint (info->kate_bytesout * 8. / info->katetime * .001);
    if (info->kkbps<0)
        info->kkbps=0;
    print_stats(info, info->katetime);
    */
}

static int find_best_valid_kate_page(oggmux_info *info)
{
    int n;
    double t=0.0;
    int best=-1;
    if (info->with_kate) for (n=0; n<info->n_kate_streams;++n) {
        oggmux_kate_stream *ks=info->kate_streams+n;
        if (ks->katepage_valid) {
          if (best==-1 || ks->katetime<t) {
            t=ks->katetime;
            best=n;
          }
        }
    }
    return best;
}

void oggmux_flush (oggmux_info *info, int e_o_s)
{
    int n,len;
    ogg_page og;
    int best;

    if (info->passno==1) {
        print_stats(info, info->videotime);
        return;
    }
    /* flush out the ogg pages to info->outfile */
    while (1) {
        /* Get pages for both streams, if not already present, and if available.*/
        if (!info->audio_only && !info->videopage_valid) {
            // this way seeking is much better,
            // not sure if 23 packets  is a good value. it works though
            int v_next=0;
            if (info->v_pkg>22 && ogg_stream_flush(&info->to, &og)) {
                v_next=1;
            }
            else if (ogg_stream_pageout(&info->to, &og)) {
                v_next=1;
            }
            if (v_next) {
                len = og.header_len + og.body_len;
                if (info->videopage_buffer_length < len) {
                    info->videopage = realloc(info->videopage, len);
                    info->videopage_buffer_length = len;
                }
                info->videopage_len = len;
                memcpy(info->videopage, og.header, og.header_len);
                memcpy(info->videopage+og.header_len , og.body, og.body_len);

                info->videopage_valid = 1;
                if (ogg_page_granulepos(&og)>0) {
                    info->videotime = th_granule_time(info->td, ogg_page_granulepos(&og));
                }
            }
        }
        if (!info->video_only && !info->audiopage_valid) {
            // this way seeking is much better,
            // not sure if 23 packets  is a good value. it works though
            int a_next=0;
            if (info->a_pkg>22 && ogg_stream_flush(&info->vo, &og)) {
                a_next=1;
            }
            else if (ogg_stream_pageout(&info->vo, &og)) {
                a_next=1;
            }
            if (a_next) {
                len = og.header_len + og.body_len;
                if (info->audiopage_buffer_length < len) {
                    info->audiopage = realloc(info->audiopage, len);
                    info->audiopage_buffer_length = len;
                }
                info->audiopage_len = len;
                memcpy(info->audiopage, og.header, og.header_len);
                memcpy(info->audiopage+og.header_len , og.body, og.body_len);

                info->audiopage_valid = 1;
                if (ogg_page_granulepos(&og)>0) {
                    info->audiotime= vorbis_granule_time (&info->vd, ogg_page_granulepos(&og));
                }
            }
        }

#ifdef HAVE_KATE
        if (info->with_kate) for (n=0; n<info->n_kate_streams; ++n) {
            oggmux_kate_stream *ks=info->kate_streams+n;
            if (!ks->katepage_valid) {
                int k_next=0;
                /* always flush kate stream */
                if (ogg_stream_flush(&ks->ko, &og) > 0) {
                    k_next = 1;
                }
                if (k_next) {
                    len = og.header_len + og.body_len;
                    if (ks->katepage_buffer_length < len) {
                        ks->katepage = realloc(ks->katepage, len);
                        ks->katepage_buffer_length = len;
                    }
                    ks->katepage_len = len;
                    memcpy(ks->katepage, og.header, og.header_len);
                    memcpy(ks->katepage+og.header_len , og.body, og.body_len);

                    ks->katepage_valid = 1;
                    if (ogg_page_granulepos(&og)>0) {
                        ks->katetime= kate_granule_time (&ks->ki,
                            ogg_page_granulepos(&og));
                    }
                }
            }
        }
#endif

#ifdef HAVE_KATE
#define CHECK_KATE_OUTPUT(which) \
        if (best >= 0 && info->kate_streams[best].katetime <= info->which##time) { \
            write_kate_page(info, best); \
            continue; \
        }
#else
#define CHECK_KATE_OUTPUT(which) ((void)0)
#endif

        best=find_best_valid_kate_page(info);

        if (info->video_only && info->videopage_valid) {
            CHECK_KATE_OUTPUT(video);
            write_video_page(info);
        }
        else if (info->audio_only && info->audiopage_valid) {
            CHECK_KATE_OUTPUT(audio);
            write_audio_page(info);
        }
        /* We're using both. We can output only:
        *  a) If we have valid pages for both
        *  b) At EOS, for the remaining stream.
        */
        else if (info->videopage_valid && info->audiopage_valid) {
            /* Make sure they're in the right order. */
            if (info->videotime <= info->audiotime) {
              CHECK_KATE_OUTPUT(video);
              write_video_page(info);
            }
            else {
              CHECK_KATE_OUTPUT(audio);
              write_audio_page(info);
            }
        }
        else if (e_o_s && best>=0) {
            write_kate_page(info, best);
        }
        else if (e_o_s && info->videopage_valid) {
            write_video_page(info);
        }
        else if (e_o_s && info->audiopage_valid) {
            write_audio_page(info);
        }
        else {
            break; /* Nothing more writable at the moment */
        }
    }
}

void oggmux_close (oggmux_info *info) {
    int n;

    if (!info->audio_only) {
        th_info_clear(&info->ti);
    }

    print_stats(info, info->duration);

    ogg_stream_clear (&info->vo);
    vorbis_block_clear (&info->vb);
    vorbis_dsp_clear (&info->vd);
    vorbis_info_clear (&info->vi);

    ogg_stream_clear (&info->to);
    th_encode_free (info->td);
    if (info->passno!=1) {
        vorbis_comment_clear (&info->vc);
        th_comment_clear (&info->tc);
    }

#ifdef HAVE_KATE
    if (info->with_kate && info->passno!=1) {
      for (n=0; n<info->n_kate_streams; ++n) {
        ogg_stream_clear (&info->kate_streams[n].ko);
        kate_comment_clear (&info->kate_streams[n].kc);
        kate_info_clear (&info->kate_streams[n].ki);
        kate_clear (&info->kate_streams[n].k);
      }
    }
#endif

    if (info->with_skeleton)
        ogg_stream_clear (&info->so);

    if (info->passno!=1 && info->outfile && info->outfile != stdout)
        fclose (info->outfile);

    if (info->videopage)
        free(info->videopage);
    if (info->audiopage)
        free(info->audiopage);

    for (n=0; n<info->n_kate_streams; ++n) {
        if (info->kate_streams[n].katepage)
            free(info->kate_streams[n].katepage);
    }
    free(info->kate_streams);
}

