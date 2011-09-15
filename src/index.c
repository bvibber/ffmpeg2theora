/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * index.c -- Stores info about keyframes for indexing.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include "index.h"

#include "theorautils.h"
 
#ifndef INT64_MAX
#define INT64_MAX (~((ogg_int64_t)1 << 63))
#endif

#ifndef INT64_MIN
#define INT64_MIN ((ogg_int64_t)1 << 63)
#endif


void seek_index_init(seek_index* index, int packet_interval)
{
    if (!index)
        return;
    memset(index, 0, sizeof(seek_index));
    index->prev_packet_time = INT64_MIN;
    index->packet_interval = packet_interval;
    index->start_time = INT64_MAX;
    index->end_time = INT64_MIN;
    index->packet_size = -1;
}

void seek_index_clear(seek_index* index)
{
    if (!index)
        return;
    if (index->packet_capacity && index->packets) {
        free(index->packets);
        index->packets = 0;
        index->packet_capacity = 0;
    }
    if (index->pages_capacity && index->pages) {
        free(index->pages);
        index->pages = 0;
        index->pages_capacity = 0;
    }
}

/*
 * Ensures that |*pointer|, which points to |capacity| elements of size
 * |element_size|, can contain |target_capacity| elements. This will realloc
 * |*pointer| if necessary, updating |*capacity| when it does so.
 * Returns 0 on success, -1 on failure (OOM).
 */
static int ensure_capacity(int* capacity,
                           int target_capacity,
                           size_t element_size,
                           void** pointer)
{
    size_t size = 0;
    ogg_int64_t new_capacity;

    if (*capacity > target_capacity) {
        /* We have capacity to accommodate the increase. No need to resize. */
        return 0;
    }

    /* Not enough capacity to accommodate increase, resize.
     * Expand by 3/2 + 1. */
    new_capacity = *capacity;
    while (new_capacity >= 0 && new_capacity <= target_capacity) {
        new_capacity = (new_capacity * 3) / 2 + 1;
    }
    if (new_capacity < 0 ||
        new_capacity > INT_MAX ||
        new_capacity * element_size > INT_MAX)
    {
        /* Integer overflow or otherwise ridiculous size. Fail. */
        return -1;
    }
    size = (size_t)new_capacity * element_size;
    *pointer = realloc(*pointer, size);
    if (!*pointer) {
        return -1;
    }
    *capacity = new_capacity;
    return 0;
}

/*
 * Returns 0 on success, -1 on failure.
 */
int seek_index_record_sample(seek_index* index,
                             int packetno,
                             ogg_int64_t start_time,
                             ogg_int64_t end_time,
                             int is_keyframe)
{
    keyframe_packet* packet;

    /* Update the end/start times, so we know the extremes of the
       indexed range. */
    if (start_time < index->start_time) {
        index->start_time = start_time;
    }
    if (end_time > index->end_time) {
        index->end_time = end_time;
    }

    if (!is_keyframe) {
        /* Sample is not a keyframe, don't add it to the index. */
        return 0;
    }

    if (ensure_capacity(&index->packet_capacity,
                        index->packet_num + 1,
                        sizeof(keyframe_packet),
                        (void**)&index->packets) != 0)
    {
        /* Can't increase array size, probably OOM. */
        return -1;
    }
    packet = &index->packets[index->packet_num];
    packet->packetno = packetno;
    packet->start_time = start_time;
    index->packet_num++;
    index->prev_packet_time = start_time;

    return 0;
}

/*
 * Returns 0 on success, -1 on failure.
 */
int seek_index_record_page(seek_index* index,
                           ogg_int64_t offset,
                           int packet_start_num)
{
    keyframe_page* page;
    if (ensure_capacity(&index->pages_capacity,
                        index->pages_num + 1,
                        sizeof(keyframe_page),
                        (void**)&index->pages) != 0)
    {
        /* Can't increase array size, probably OOM. */
        return -1;
    }
    page = &index->pages[index->pages_num];
    page->offset = offset;
    page->packet_start_num = packet_start_num;
    index->pages_num++;
    return 0;
}

void seek_index_set_max_keypoints(seek_index* index, int max_keypoints)
{
    index->max_keypoints = max_keypoints;
}

