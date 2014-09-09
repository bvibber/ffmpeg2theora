/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * avinfo.c -- Convert ffmpeg supported a/v files to  Ogg Theora / Vorbis
 * Copyright (C) 2003-2011 <j@v2v.cc>
 *
 *   gcc -o avinfo avinfo.c -DAVINFO `pkg-config --cflags --libs libavcodec libavformat`
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>

#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"

#ifndef WIN32
#if !defined(off64_t)
#define off64_t off_t
#endif
#endif

#ifdef WIN32
#define fseeko fseeko64
#define ftello ftello64
#endif

#ifdef _BIG_ENDIAN
#define htonll(x) \
((((x) & 0xff00000000000000LL) >> 56) | \
(((x) & 0x00ff000000000000LL) >> 40) | \
(((x) & 0x0000ff0000000000LL) >> 24) | \
(((x) & 0x000000ff00000000LL) >> 8) | \
(((x) & 0x00000000ff000000LL) << 8) | \
(((x) & 0x0000000000ff0000LL) << 24) | \
(((x) & 0x000000000000ff00LL) << 40) | \
(((x) & 0x00000000000000ffLL) << 56))
#else
#define htonll(x) x
#endif

unsigned long long get_filesize(char const *filename) {
    unsigned long long size = 0;
    FILE *file = fopen(filename, "rb");
    if (file) {
        fseeko(file, 0, SEEK_END);
        size = ftello(file);
        fclose(file);
    }
    return size;
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
    else if (!strcmp(codec_name, "libvpx")) {
        codec_name = "vp8";
    }
   return codec_name;
}

char *replace_str_all(char *str, char *orig, char *rep) {
  const char buffer[4096];
  char *p, *p_str = str, *p_buffer = (char *)buffer;
  int len = strlen(str);
  strncpy(p_buffer, str, len);
  while ((p = strstr(p_str, orig))) {
    strncpy(p_buffer, p_str, p-p_str);
    p_buffer += (p-p_str);
    len = len - strlen(orig) + strlen(rep);    
    sprintf(p_buffer, "%s%s", rep, p+strlen(orig));
    p_str = p + strlen(orig);
    p_buffer += strlen(rep);
  }
  p = malloc(len+1);
  strncpy(p, buffer, len);
  p[len] = '\0';
  return p;
}

enum {
    JSON_STRING,
    JSON_INT,
    JSON_LONGLONG,
    JSON_FLOAT,
} JSON_TYPES;

static void do_indent(FILE *output, int indent) {
    int i;
    for (i = 0; i < indent; i++)
        fprintf(output, "  ");
}

void json_add_key_value(FILE *output, char *key, void *value, int type, int last, int indent) {
    char *p;
    
    do_indent(output, indent);
    switch(type) {
        case JSON_STRING:
            p = (char *)value;
            p = replace_str_all(p, "\\", "\\\\");
            p = replace_str_all(p, "\"", "\\\"");
            fprintf(output, "\"%s\": \"%s\"", key, p);
            free(p);
            break;
        case JSON_INT:
            fprintf(output, "\"%s\": %d", key, *(int *)value);
            break;
        case JSON_LONGLONG:
            fprintf(output, "\"%s\": %lld", key, *(unsigned long long *)value);
            break;
        case JSON_FLOAT:
            fprintf(output, "\"%s\": %f", key, *(float *)value);
            break;
    }
    if (last) {
        fprintf(output, "\n");
    } else {
        fprintf(output, ",\n");
    }
}

void json_codec_info(FILE *output, AVCodecContext *enc, int indent) {
    const char *codec_name;
    char buf1[32];
    int bitrate;
    AVRational display_aspect_ratio;

    codec_name  = avcodec_get_name(enc->codec_id);

    switch(enc->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        codec_name = fix_codec_name(codec_name);
        json_add_key_value(output, "codec", (void *)codec_name, JSON_STRING, 0, indent);
        if (enc->pix_fmt != PIX_FMT_NONE) {
            json_add_key_value(output, "pixel_format", (void *)av_get_pix_fmt_name(enc->pix_fmt), JSON_STRING, 0, indent);
        }
        if (enc->width) {
            json_add_key_value(output, "width", &enc->width, JSON_INT, 0, indent);
            json_add_key_value(output, "height", &enc->height, JSON_INT, 0, indent);
            if (enc->sample_aspect_ratio.num) {
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          enc->width*enc->sample_aspect_ratio.num,
                          enc->height*enc->sample_aspect_ratio.den,
                          1024*1024);
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         enc->sample_aspect_ratio.num, enc->sample_aspect_ratio.den);
                json_add_key_value(output, "pixel_aspect_ratio", buf1, JSON_STRING, 0, indent);
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         display_aspect_ratio.num, display_aspect_ratio.den);
                json_add_key_value(output, "display_aspect_ratio", buf1, JSON_STRING, 0, indent);
            }
        }
        bitrate = enc->bit_rate;
        if (bitrate != 0) {
            float t = (float)bitrate / 1000;
            json_add_key_value(output, "bitrate", &t, JSON_FLOAT, 0, indent);
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        codec_name = fix_codec_name(codec_name);
        json_add_key_value(output, "codec", (void *)codec_name, JSON_STRING, 0, indent);
        if (enc->sample_rate) {
            json_add_key_value(output, "samplerate", &enc->sample_rate, JSON_INT, 0, indent);
        }
        json_add_key_value(output, "channels", &enc->channels, JSON_INT, 0, indent);

        /* for PCM codecs, compute bitrate directly */
        switch(enc->codec_id) {
        case AV_CODEC_ID_PCM_F64BE:
        case AV_CODEC_ID_PCM_F64LE:
            bitrate = enc->sample_rate * enc->channels * 64;
            break;
        case AV_CODEC_ID_PCM_S32LE:
        case AV_CODEC_ID_PCM_S32BE:
        case AV_CODEC_ID_PCM_U32LE:
        case AV_CODEC_ID_PCM_U32BE:
        case AV_CODEC_ID_PCM_F32BE:
        case AV_CODEC_ID_PCM_F32LE:
            bitrate = enc->sample_rate * enc->channels * 32;
            break;
        case AV_CODEC_ID_PCM_S24LE:
        case AV_CODEC_ID_PCM_S24BE:
        case AV_CODEC_ID_PCM_U24LE:
        case AV_CODEC_ID_PCM_U24BE:
        case AV_CODEC_ID_PCM_S24DAUD:
            bitrate = enc->sample_rate * enc->channels * 24;
            break;
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_S16LE_PLANAR:
        case AV_CODEC_ID_PCM_U16LE:
        case AV_CODEC_ID_PCM_U16BE:
            bitrate = enc->sample_rate * enc->channels * 16;
            break;
        case AV_CODEC_ID_PCM_S8:
        case AV_CODEC_ID_PCM_U8:
        case AV_CODEC_ID_PCM_ALAW:
        case AV_CODEC_ID_PCM_MULAW:
        case AV_CODEC_ID_PCM_ZORK:
            bitrate = enc->sample_rate * enc->channels * 8;
            break;
        default:
            bitrate = enc->bit_rate;
            break;
        }
        if (bitrate != 0) {
            float t = (float)bitrate / 1000;
            json_add_key_value(output, "bitrate", &t, JSON_FLOAT, 0, indent);
        }
        break;
    /*
    case AVMEDIA_TYPE_DATA:
        fprintf(output, "datacodec: %s\n", codec_name);
        bitrate = enc->bit_rate;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        fprintf(output, "subtitle: %s\n", codec_name);
        bitrate = enc->bit_rate;
        break;
    case AVMEDIA_TYPE_ATTACHMENT:
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

static int utf8_validate (char *s, int n) {
  int i;
  int extra_bytes;
  int mask;

  i=0;
  while (i<n) {
    if (i < n-3 && (*(uint32_t *)(s+i) & 0x80808080) == 0) {
      i+=4;
      continue;
    }
    if (s[i] < 0) goto error;
    if (s[i] < 128) {
      i++;
      continue;
    }
    if ((s[i] & 0xe0) == 0xc0) {
      extra_bytes = 1;
      mask = 0x7f;
    } else if ((s[i] & 0xf0) == 0xe0) {
      extra_bytes = 2;
      mask = 0x1f;
    } else if ((s[i] & 0xf8) == 0xf0) {
      extra_bytes = 3;
      mask = 0x0f;
    } else {
      goto error;
    }
    if (i + extra_bytes >= n) goto error;
    while(extra_bytes--) {
      i++;
      if ((s[i] & 0xc0) != 0x80) goto error;
    }
    i++;
  }

error:
  return i == n;
}


void json_metadata(FILE *output, AVDictionary *m, int indent)
{
    int first = 1;
    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (strlen(tag->value) && utf8_validate (tag->value, strlen(tag->value))) {
            if (first) {
                first = 0;
                do_indent(output, indent);
                fprintf(output, "\"metadata\": {\n");
                do_indent(output, indent + 1);
            } else {
                do_indent(output, indent + 1);
                fprintf(output, ",");
            }
            json_add_key_value(output, tag->key, tag->value, JSON_STRING, 1, 0);
        }
    }
    if (!first) {
        do_indent(output, indent);
        fprintf(output, "},\n");
    }
}



static void json_stream_format(FILE *output, AVFormatContext *ic, int i, int indent, int first, int type_filter) {
    static int _first = 1;
    char buf1[32];

    AVStream *st = ic->streams[i];

    if (first)
        _first = 1;

    if(st->codec->codec_type == type_filter){
        if (!_first)
            fprintf(output, ", ");
        _first = 0;
        fprintf(output, "{\n");

        json_codec_info(output, st->codec, indent + 1);
        if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            if (st->time_base.den && st->time_base.num && av_q2d(st->time_base) > 0.001) {
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         st->time_base.den, st->time_base.num);
                json_add_key_value(output, "framerate", buf1, JSON_STRING, 0, indent + 1);
            } else {
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         st->avg_frame_rate.num, st->avg_frame_rate.den);
                json_add_key_value(output, "framerate", buf1, JSON_STRING, 0, indent + 1);
            }
            if (st->sample_aspect_ratio.num && // default
                av_cmp_q(st->sample_aspect_ratio, st->codec->sample_aspect_ratio)) {
                AVRational display_aspect_ratio;
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          st->codec->width*st->sample_aspect_ratio.num,
                          st->codec->height*st->sample_aspect_ratio.den,
                          1024*1024);
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
                json_add_key_value(output, "pixel_aspect_ratio", buf1, JSON_STRING, 0, indent+1);
                snprintf(buf1, sizeof(buf1), "%d:%d",
                         display_aspect_ratio.num, display_aspect_ratio.den);
                json_add_key_value(output, "display_aspect_ratio", buf1, JSON_STRING, 0, indent+1);
            }
        }
        json_metadata(output, st->metadata, indent + 1);
        json_add_key_value(output, "id", &i, JSON_INT, 1, indent + 1);
        do_indent(output, indent-1);
        fprintf(output, "}");
    }
}

/* 
 * os hash
 * based on public domain example from
 * http://trac.opensubtitles.org/projects/opensubtitles/wiki/HashSourceCodes   
 * 
 * plus modification for files < 64k, buffer is filled with file data and padded with 0
 */

unsigned long long gen_oshash(char const *filename) {
    FILE *file;
    int i;
    unsigned long long t1=0;
    unsigned long long buffer1[8192*2];
    int used = 8192*2;
    file = fopen(filename, "rb");
    if (file) {
        //add filesize
        fseeko(file, 0, SEEK_END);
        t1 = ftello(file);
        fseeko(file, 0, SEEK_SET);

        if(t1 < 65536) {
            used = t1/8;
            fread(buffer1, used, 8, file);
        } else {
            fread(buffer1, 8192, 8, file);
            fseeko(file, -65536, SEEK_END);
            fread(&buffer1[8192], 8192, 8, file); 
        }
        for (i=0; i < used; i++)
            t1+=htonll(buffer1[i]);
        fclose(file);
    }
    return t1;
}

void json_oshash(FILE *output, char const *filename, int indent) {
    char hash[32];
#ifdef WIN32
    sprintf(hash,"%016I64x", gen_oshash(filename));
#elif defined (__SVR4) && defined (__sun)
    sprintf(hash,"%016llx", gen_oshash(filename));
#else
    sprintf(hash,"%016qx", gen_oshash(filename));
#endif
    if (strcmp(hash,"0000000000000000") > 0)
        json_add_key_value(output, "oshash", (void *)hash, JSON_STRING, 0, indent);
}


/* "user interface" functions */
void json_format_info(FILE* output, AVFormatContext *ic, const char *url) {
    int i;
    unsigned long long filesize;

    fprintf(output, "{\n");
    if(ic) {
        if (ic->duration != AV_NOPTS_VALUE) {
            float secs;
            secs = (float)ic->duration / AV_TIME_BASE;
            json_add_key_value(output, "duration", &secs, JSON_FLOAT, 0, 1);
        } else {
            float t = -1;
            json_add_key_value(output, "duration", &t, JSON_FLOAT, 0, 1);
        }
        if (ic->bit_rate) {
            float t = (float)ic->bit_rate / 1000;
            json_add_key_value(output, "bitrate", &t, JSON_FLOAT, 0, 1);
        }

        do_indent(output, 1);
        fprintf(output, "\"video\": [");
        if(ic->nb_programs) {
            int j, k;
            for(j=0; j<ic->nb_programs; j++) {
                for(k=0; k<ic->programs[j]->nb_stream_indexes; k++)
                    json_stream_format(output, ic, ic->programs[j]->stream_index[k], 2, !k && !j, AVMEDIA_TYPE_VIDEO);
             }
        } else {
            for(i=0;i<ic->nb_streams;i++) {
                json_stream_format(output, ic, i, 2, !i, AVMEDIA_TYPE_VIDEO);
            }
        }
        fprintf(output, "],\n");

        do_indent(output, 1);
        fprintf(output, "\"audio\": [");
        if(ic->nb_programs) {
            int j, k;
            for(j=0; j<ic->nb_programs; j++) {
                for(k=0; k<ic->programs[j]->nb_stream_indexes; k++)
                    json_stream_format(output, ic, ic->programs[j]->stream_index[k], 2, !k && !j, AVMEDIA_TYPE_AUDIO);
             }
        } else {
            for(i=0;i<ic->nb_streams;i++) {
                json_stream_format(output, ic, i, 2, !i, AVMEDIA_TYPE_AUDIO);
            }
        }
        fprintf(output, "],\n");
        json_metadata(output, ic->metadata, 1);
    } else {
        json_add_key_value(output, "code", "badfile", JSON_STRING, 0, 1);
        json_add_key_value(output, "error", "file does not exist or has unknown format.", JSON_STRING, 0, 1);
    }
    json_oshash(output, url, 1);
    json_add_key_value(output, "path", (void *)url, JSON_STRING, 0, 1);

    filesize = get_filesize(url);
    json_add_key_value(output, "size", &filesize, JSON_LONGLONG, 1, 1);

    fprintf(output, "}\n");
}

#ifdef AVINFO
int main(int argc, char **argv) {
    char inputfile_name[255];
    AVFormatContext *context = NULL;
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
    
    if (avformat_open_input(&context, inputfile_name, NULL, NULL) >= 0) {
        if (avformat_find_stream_info(context, NULL) >= 0) {
            json_format_info(output, context, inputfile_name);
        }
    }
    if(output != stdout) {
        fclose(output);
    }
}
#endif
