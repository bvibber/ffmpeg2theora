/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * index.h -- Stores info about keyframes for indexing.
 * Copyright (C) 2009 Mozilla Foundation.
 *
 * Contributed by Chris Pearce <chris@pearce.org.nz>.
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

#ifndef __INDEX_H__
#define __INDEX_H__

#include <ogg/os_types.h>


/* Records the packetno and end time of a keyframe's packet in an ogg
   stream. */
typedef struct {
    int packetno;
    ogg_int64_t start_time; /* in ms */
}
keyframe_packet;


/* Records the geometry of pages in an ogg stream. From this we can reconstruct
   the offset of the start of page in which any arbitrary packet starts. */
typedef struct {

    /* Byte offset of the start of the page. */
    ogg_int64_t offset;

    /* Number of packets that start on this page. */
    int packet_start_num;
}
keyframe_page;

/* Holds data relating to the keyframes in a stream, and the pages on which
   the keyframes reside. */
typedef struct {

    /* Array of keyframe packets we've discovered in this stream. */
    keyframe_packet* packets;
    /* Numeber of allocated elements in |packets|. */
    int packet_capacity;
    /* Number of used elements in |packets|. */
    int packet_num;

    /* The end time of the previous keyframe packet added to the index. */
    ogg_int64_t prev_packet_time;

    /* Minimum time allowed between packets, in milliseconds. */
    ogg_int64_t packet_interval;

    /* Pages encoded into this stream. */
    keyframe_page* pages;
    /* Number of allocated elements in |pages|. */
    int pages_capacity;
    /* Number of used elements in |pages|. */
    int pages_num;
    
    /* Number of keypoints allocated in the placeholder index packet
       on disk. */
    int max_keypoints;

    /* Byte offset of page which stores this index in the file. */
    ogg_int64_t page_location;

    /* The start time of the first sample in the stream, in ms. */
    ogg_int64_t start_time;

    /* The end time of the last sample in the stream, in ms. */
    ogg_int64_t end_time;
    
    /* The size we'll reserve for the index packet on disk. */
    unsigned int packet_size;
}
seek_index;


/* Initialize index to have a minimum of |packet_interval| ms between
   keyframes. */
void seek_index_init(seek_index* index, int packet_interval);

/* Frees all memory associated with an index. */
void seek_index_clear(seek_index* index);

/* Records the packetno of a sample in an index, with corresponding 
   start and end times. Returns 0 on success, -1 on failure. */
int seek_index_record_sample(seek_index* index,
                             int packetno,
                             ogg_int64_t start_time,
                             ogg_int64_t end_time,
                             int is_keyframe);

/* Returns 0 on success, -1 on failure. */
int seek_index_record_page(seek_index* index,
                           ogg_int64_t offset,
                           int packet_start_num);

/* Sets maximum number of keypoints we'll allowe in an index. This sets
   the size of the index packet, and its value can be estimated once the
   media's duration is known. */
void seek_index_set_max_keypoints(seek_index* index, int num_keypoints);


#endif
