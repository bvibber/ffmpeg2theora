/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * ffmpeg2theora.c -- Convert ffmpeg supported a/v files to Ogg Theora / Ogg Vorbis
 * Copyright (C) 2003-2008 <j@v2v.cc>
 *
 * gcc -o avinfo avinfo.c -DAVINFO `pkg-config --cflags --libs libavcodec libavformat`
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
#include <errno.h>
#include <sys/stat.h>

#include "libavformat/avformat.h"

long get_filesize(char const *filename) {
    struct stat file;
    if(!stat(filename, &file)) {
        return file.st_size;
    }
    return 0;
}

char const *fix_codec_name(char const *codec_name) {
    if (!strcmp(codec_name, "libschrodinger")) {
        codec_name = "dirac";
    }
    else if (!strcmp(codec_name, "vp6f")) {
        codec_name = "vp6";
    }
    else if (!strcmp(codec_name, "mpeg2video")) {
        codec_name = "mpeg2";
    }
    else if (!strcmp(codec_name, "mpeg1video")) {
        codec_name = "mpeg1";
    }
    else if (!strcmp(codec_name, "0x0000")) {
        codec_name = "mu-law";
    }
   return codec_name;
}

enum {
    JSON_STRING,
    JSON_INT,
    JSON_LONG,
    JSON_FLOAT,
} JSON_TYPES;

void json_add_key_value(FILE *output, char *key, void *value, int type, int last) {
    char *p, *pp;
    switch(type) {
        case JSON_STRING:
            p = (char *)value;
            fprintf(output, "  \"%s\": \"", key);
            while(pp = strchr(p, 34)) {
                *pp = '\0';
                fprintf(output, "%s\\\"", p);
                p = pp + 1;
            }
            fprintf(output, "%s\"", p);
            break;
        case JSON_INT:
            fprintf(output, "  \"%s\": %d", key, *(int *)value);
            break;
        case JSON_LONG:
            fprintf(output, "  \"%s\": %ld", key, *(long *)value);
            break;
        case JSON_FLOAT:
            fprintf(output, "  \"%s\": %f", key, *(float *)value);
            break;
    }
    if (last) {
        fprintf(output, "\n");
    } else {
        fprintf(output, ",\n");
    }
}

void json_codec_info(FILE *output, AVCodecContext *enc) {
    const char *codec_name;
    AVCodec *p;
    char buf1[32];
    int bitrate;
    AVRational display_aspect_ratio;

    p = avcodec_find_decoder(enc->codec_id);

    if (p) {
        codec_name = p->name;
    } else if (enc->codec_id == CODEC_ID_MPEG2TS) {
        /* fake mpeg2 transport stream codec (currently not
           registered) */
        codec_name = "mpeg2ts";
    } else if (enc->codec_name[0] != '\0') {
        codec_name = enc->codec_name;
    } else {
        /* output avi tags */
        if(   isprint(enc->codec_tag&0xFF) && isprint((enc->codec_tag>>8)&0xFF)
           && isprint((enc->codec_tag>>16)&0xFF) && isprint((enc->codec_tag>>24)&0xFF)){
            snprintf(buf1, sizeof(buf1), "%c%c%c%c / 0x%04X",
                     enc->codec_tag & 0xff,
                     (enc->codec_tag >> 8) & 0xff,
                     (enc->codec_tag >> 16) & 0xff,
                     (enc->codec_tag >> 24) & 0xff,
                      enc->codec_tag);
        } else {
            snprintf(buf1, sizeof(buf1), "0x%04x", enc->codec_tag);
        }
        codec_name = buf1;
    }

    switch(enc->codec_type) {
    case CODEC_TYPE_VIDEO:
        codec_name = fix_codec_name(codec_name);
        json_add_key_value(output, "video_codec", (void *)codec_name, JSON_STRING, 0);
        if (enc->pix_fmt != PIX_FMT_NONE) {
            json_add_key_value(output, "pixel_format", (void *)avcodec_get_pix_fmt_name(enc->pix_fmt), JSON_STRING, 0);
        }
        if (enc->width) {
            json_add_key_value(output, "width", &enc->width, JSON_INT, 0);
            json_add_key_value(output, "height", &enc->height, JSON_INT, 0);
            if (enc->sample_aspect_ratio.num) {
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          enc->width*enc->sample_aspect_ratio.num,
                          enc->height*enc->sample_aspect_ratio.den,
                          1024*1024);
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         enc->sample_aspect_ratio.num, enc->sample_aspect_ratio.den);
                json_add_key_value(output, "pixel_aspect_ratio", buf1, JSON_STRING, 0);
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         display_aspect_ratio.num, display_aspect_ratio.den);
                json_add_key_value(output, "display_aspect_ratio", buf1, JSON_STRING, 0);
            }
        }
        bitrate = enc->bit_rate;
        if (bitrate != 0) {
            float t = (float)bitrate / 1000;
            json_add_key_value(output, "video_bitrate", &t, JSON_FLOAT, 0);
        }
        break;
    case CODEC_TYPE_AUDIO:
        codec_name = fix_codec_name(codec_name);
        json_add_key_value(output, "audio_codec", (void *)codec_name, JSON_STRING, 0);
        if (enc->sample_rate) {
            json_add_key_value(output, "samplerate", &enc->sample_rate, JSON_INT, 0);
        }
        json_add_key_value(output, "channels", &enc->channels, JSON_INT, 0);

        /* for PCM codecs, compute bitrate directly */
        switch(enc->codec_id) {
        case CODEC_ID_PCM_F64BE:
        case CODEC_ID_PCM_F64LE:
            bitrate = enc->sample_rate * enc->channels * 64;
            break;
        case CODEC_ID_PCM_S32LE:
        case CODEC_ID_PCM_S32BE:
        case CODEC_ID_PCM_U32LE:
        case CODEC_ID_PCM_U32BE:
        case CODEC_ID_PCM_F32BE:
        case CODEC_ID_PCM_F32LE:
            bitrate = enc->sample_rate * enc->channels * 32;
            break;
        case CODEC_ID_PCM_S24LE:
        case CODEC_ID_PCM_S24BE:
        case CODEC_ID_PCM_U24LE:
        case CODEC_ID_PCM_U24BE:
        case CODEC_ID_PCM_S24DAUD:
            bitrate = enc->sample_rate * enc->channels * 24;
            break;
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_S16LE_PLANAR:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            bitrate = enc->sample_rate * enc->channels * 16;
            break;
        case CODEC_ID_PCM_S8:
        case CODEC_ID_PCM_U8:
        case CODEC_ID_PCM_ALAW:
        case CODEC_ID_PCM_MULAW:
        case CODEC_ID_PCM_ZORK:
            bitrate = enc->sample_rate * enc->channels * 8;
            break;
        default:
            bitrate = enc->bit_rate;
            break;
        }
        if (bitrate != 0) {
            float t = (float)bitrate / 1000;
            json_add_key_value(output, "audio_bitrate", &t, JSON_FLOAT, 0);
        }
        break;
    /*
    case CODEC_TYPE_DATA:
        fprintf(output, "datacodec: %s\n", codec_name);
        bitrate = enc->bit_rate;
        break;
    case CODEC_TYPE_SUBTITLE:
        fprintf(output, "subtitle: %s\n", codec_name);
        bitrate = enc->bit_rate;
        break;
    case CODEC_TYPE_ATTACHMENT:
        fprintf(output, "attachment: : %s\n", codec_name);
        bitrate = enc->bit_rate;
        break;
    */
    default:
        //FIXME: ignore unkown for now
        /*
        snprintf(buf, buf_size, "Invalid Codec type %d", enc->codec_type);
        */
        return;
    }
}


static void json_stream_format(FILE *output, AVFormatContext *ic, int i) {
    char buf[1024];
    char buf1[32];
    int flags = ic->iformat->flags;
    AVStream *st = ic->streams[i];
    int g = av_gcd(st->time_base.num, st->time_base.den);
    AVMetadataTag *lang = av_metadata_get(st->metadata, "language", NULL, 0);
    json_codec_info(output, st->codec);
    if (st->sample_aspect_ratio.num && // default
        av_cmp_q(st->sample_aspect_ratio, st->codec->sample_aspect_ratio)) {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  st->codec->width*st->sample_aspect_ratio.num,
                  st->codec->height*st->sample_aspect_ratio.den,
                  1024*1024);
        snprintf(buf1, sizeof(buf1), "%d:%d",
                 st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
        json_add_key_value(output, "pixel_aspect_ratio", buf1, JSON_STRING, 0);
        snprintf(buf1, sizeof(buf1), "%d:%d",
                 display_aspect_ratio.num, display_aspect_ratio.den);
        json_add_key_value(output, "display_aspect_ratio", buf1, JSON_STRING, 0);
    }
    if(st->codec->codec_type == CODEC_TYPE_VIDEO){
        if (st->time_base.den && st->time_base.num && av_q2d(st->time_base) > 0.001) {
            snprintf(buf1, sizeof(buf1), "%d:%d",
                     st->time_base.den, st->time_base.num);
            json_add_key_value(output, "framerate", buf1, JSON_STRING, 0);
        } else {
            snprintf(buf1, sizeof(buf1), "%d:%d",
                     st->r_frame_rate.num, st->r_frame_rate.den);
            json_add_key_value(output, "framerate", buf1, JSON_STRING, 0);
        }
    }
}

/* 
 * os hash
 * based on public domain example from
 * http://trac.opensubtitles.org/projects/opensubtitles/wiki/HashSourceCodes   
 * -works only on little-endian procesor DEC, Intel and compatible
 * -sizeof(unsigned long long) must be 8
 */
unsigned long long gen_oshash(char const *filename) {
    FILE *file;
    int i;
    unsigned long long t1=0;
    unsigned long long buffer1[8192*2];


    file = fopen(filename, "rb");
    if (file) {
        fread(buffer1, 8192, 8, file);
        fseek(file, -65536, SEEK_END);
        fread(&buffer1[8192], 8192, 8, file); 
        for (i=0; i < 8192*2; i++)
            t1+=buffer1[i];
        t1+= ftell(file); //add filesize
        fclose(file);
    }
    return t1;
}

void json_oshash(FILE *output, char const *filename) {
    char hash[32];
#ifdef WIN32
    sprintf(hash,"%16I64x", gen_oshash(filename));
#else
    sprintf(hash,"%qx", gen_oshash(filename));
#endif
    json_add_key_value(output, "oshash", (void *)hash, JSON_STRING, 0);
}


/* "user interface" functions */
void json_format_info(FILE* output, AVFormatContext *ic, const char *url) {
    int i;
    long filesize;

    fprintf(output, "{\n");
    if (ic->duration != AV_NOPTS_VALUE) {
        float secs;
        secs = (float)ic->duration / AV_TIME_BASE;
        json_add_key_value(output, "duration", &secs, JSON_FLOAT, 0);
    } else {
        float t = -1;
        json_add_key_value(output, "duration", &t, JSON_FLOAT, 0);
    }
    if (ic->bit_rate) {
        float t = (float)ic->bit_rate / 1000;
        json_add_key_value(output, "bitrate", &t, JSON_FLOAT, 0);
    }

    if(ic->nb_programs) {
        int j, k;
        for(j=0; j<ic->nb_programs; j++) {
            for(k=0; k<ic->programs[j]->nb_stream_indexes; k++)
                json_stream_format(output, ic, ic->programs[j]->stream_index[k]);
         }
    } else {
        for(i=0;i<ic->nb_streams;i++) {
            json_stream_format(output, ic, i);
        }
    }
    json_oshash(output, url);
    json_add_key_value(output, "path", (void *)url, JSON_STRING, 0);
    filesize = get_filesize(url);
    json_add_key_value(output, "size", &filesize, JSON_LONG, 1);

    fprintf(output, "}\n");
}

#ifdef AVINFO
int main(int argc, char **argv) {
    char inputfile_name[255];
    AVInputFormat *input_fmt = NULL;
    AVFormatParameters *formatParams = NULL;
    AVFormatContext *context;
    FILE* output = stdout;
    
    avcodec_register_all();
    av_register_all();

    if(argc == 1) {
        fprintf(stderr, "usage: %s avfile [outputfile]\n", argv[0]);
        exit(1);
    }
    snprintf(inputfile_name, sizeof(inputfile_name),"%s", argv[1]);
    if(argc == 3) {
        output = fopen(argv[2], "w");
    }
    
    if (av_open_input_file(&context, inputfile_name, input_fmt, 0, formatParams) >= 0) {
        if (av_find_stream_info(context) >= 0) {
            json_format_info(output, context, inputfile_name);
        }
    }
    if(output != stdout) {
        fclose(output);
    }
}
#endif
