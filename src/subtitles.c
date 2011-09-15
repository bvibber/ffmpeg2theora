/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * subtitles.c -- Kate Subtitles
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>
#ifdef HAVE_ICONV
#include "iconv.h"
#endif

#include "libavformat/avformat.h"

#ifdef WIN32
#include "fcntl.h"
#endif

#include "theorautils.h"
#include "subtitles.h"


static void warn(FILE *frontend, const char *file, unsigned int line, const char *format,...)
{
  va_list ap;
  va_start(ap,format);
  if (frontend) {
    fprintf(frontend,"{\"WARNING\": \"");
    vfprintf(frontend,format,ap);
    fprintf(frontend,"\"}\n");
  }
  else {
    if (file)
      fprintf(stderr, "WARNING - %s:%u: ", file, line);
    else
      fprintf(stderr, "WARNING - ");
    vfprintf(stderr,format,ap);
    fprintf(stderr,"\n");
  }
  va_end(ap);
}

/**
  * checks whether we support the encoding
  */
int is_valid_encoding(const char *encoding)
{
#ifdef HAVE_ICONV
  iconv_t cd = iconv_open("UTF-8", encoding);
  if (cd != (iconv_t)-1) {
    iconv_close(cd);
    return 1;
  }
  return 0;
#else
  if (!strcasecmp(encoding, "UTF-8")) return 1;
  if (!strcasecmp(encoding, "UTF8")) return 1;
  if (!strcasecmp(encoding, "iso-8859-1")) return 1;
  if (!strcasecmp(encoding, "latin1")) return 1;
  return 0;
#endif
}

/**
  * adds a new kate stream structure
  */
void add_kate_stream(ff2theora this){
    ff2theora_kate_stream *ks;
    this->kate_streams=(ff2theora_kate_stream*)realloc(this->kate_streams,(this->n_kate_streams+1)*sizeof(ff2theora_kate_stream));
    ks=&this->kate_streams[this->n_kate_streams++];
    ks->filename = NULL;
    ks->num_subtitles = 0;
    ks->subtitles = 0;
    ks->stream_index = -1;
    ks->subtitles_count = 0; /* denotes not set yet */
    ks->subtitles_encoding = NULL;
    strcpy(ks->subtitles_language, "");
    strcpy(ks->subtitles_category, "");
}

/*
 * adds a stream for an embedded subtitles stream
 */
void add_subtitles_stream(ff2theora this,int stream_index,const char *language,const char *category){
  ff2theora_kate_stream *ks;
  add_kate_stream(this);

  ks = &this->kate_streams[this->n_kate_streams-1];
  ks->stream_index = stream_index;

  if (!category) category="SUB";
  strncpy(ks->subtitles_category, category, 16);
  ks->subtitles_category[15] = 0;

  if (language) {
    strncpy(ks->subtitles_language, language, 16);
    ks->subtitles_language[15] = 0;
  }
}

/*
 * sets the filename of the next subtitles file
 */
void set_subtitles_file(ff2theora this,const char *filename){
  size_t n;
  for (n=0; n<this->n_kate_streams;++n) {
    if (this->kate_streams[n].stream_index==-1 && !this->kate_streams[n].filename) break;
  }
  if (n==this->n_kate_streams) add_kate_stream(this);
  this->kate_streams[n].filename = filename;
}

/*
 * sets the language of the next subtitles file
 */
void set_subtitles_language(ff2theora this,const char *language){
  size_t n;
  for (n=0; n<this->n_kate_streams;++n) {
    if (this->kate_streams[n].stream_index==-1 && !this->kate_streams[n].subtitles_language[0]) break;
  }
  if (n==this->n_kate_streams) add_kate_stream(this);
  strncpy(this->kate_streams[n].subtitles_language, language, 16);
  this->kate_streams[n].subtitles_language[15] = 0;
}

/*
 * sets the category of the next subtitles file
 */
void set_subtitles_category(ff2theora this,const char *category){
  size_t n;
  for (n=0; n<this->n_kate_streams;++n) {
    if (this->kate_streams[n].stream_index==-1 && !this->kate_streams[n].subtitles_category[0]) break;
  }
  if (n==this->n_kate_streams) add_kate_stream(this);
  strncpy(this->kate_streams[n].subtitles_category, category, 16);
  this->kate_streams[n].subtitles_category[15] = 0;
}

/**
  * sets the encoding of the next subtitles file
  */
void set_subtitles_encoding(ff2theora this,const char *encoding){
  size_t n;
  for (n=0; n<this->n_kate_streams;++n) {
    if (this->kate_streams[n].stream_index==-1 && !this->kate_streams[n].subtitles_encoding) break;
  }
  if (n==this->n_kate_streams) add_kate_stream(this);
  this->kate_streams[n].subtitles_encoding = strdup(encoding);
}


void report_unknown_subtitle_encoding(const char *name, FILE *frontend)
{
  warn(frontend, NULL, 0, "Unknown character encoding: %s",name);
  if (!frontend) {
    fprintf(stderr, "Valid character encodings are:\n");
    fprintf(stderr, "  " SUPPORTED_ENCODINGS "\n");
  }
}

#ifdef HAVE_KATE

static char *fgets2(char *s,size_t sz,FILE *f)
{
  char *ret = fgets(s, sz, f);
  if (ret) {
    /* fixup DOS newline character */
    char *ptr=strchr(s, '\r');
    if (ptr) {
      *ptr='\n';
      *(ptr+1)=0;
    }
  }
  else *s=0;
  return ret;
}

static double hmsms2s(int h,int m,int s,int ms)
{
    return h*3600+m*60+s+ms/1000.0;
}

/* very simple implementation when no iconv */
static char *convert_subtitle_to_utf8(const char *encoding,char *text,int ignore_non_utf8, FILE *frontend)
{
  size_t nbytes;
  char *ptr;
  char *newtext = NULL;
  int errors=0;
#ifdef HAVE_ICONV
  iconv_t cd;
#endif

  if (!text) return NULL;

  if (encoding == NULL) {
     /* we don't know what encoding this is, assume UTF-8 and we'll yell if it ain't */
     encoding = "UTF-8";
  }

  if (!strcasecmp(encoding, "UTF-8") || !strcasecmp(encoding, "UTF8")) {
      /* nothing to do, already in UTF-8 */
      if (ignore_non_utf8) {
        /* actually, give the user the option of just ignoring non UTF-8 characters */
        char *wptr;
        size_t wlen0;

        nbytes = strlen(text)+1;
        newtext=(char*)malloc(nbytes);
        if (!newtext) {
          warn(frontend, NULL, 0, "Memory allocation failed - cannot convert text");
          return NULL;
        }
        ptr = text;
        wptr = newtext;
        wlen0 = nbytes;
        while (nbytes>0) {
          int ret=kate_text_get_character(kate_utf8, (const char ** const)&ptr, &nbytes);
          if (ret>=0) {
            /* valid character */
            ret=kate_text_set_character(kate_utf8, ret, &wptr, &wlen0);
            if (ret<0) {
              warn(frontend, NULL, 0, "Failed to filter utf8 text: %s", text);
              free(newtext);
              return NULL;
            }
            if (ret==0) break;
          }
          else {
            /* skip offending byte - we can't skip the terminating zero as we do byte by byte */
            ++errors;
            ++ptr;
            --nbytes;
          }
        }

        if (errors) {
          warn(frontend, NULL, 0, "Found non utf8 character(s) in string %s, scrubbed out", text);
        }
      }
      else {
        newtext = strdup(text);
      }

      return newtext;
  }

  /* now, we can either use iconv, or convert ISO-8859-1 by hand (so to speak) */
#ifdef HAVE_ICONV
  /* create a conversion for each string, it avoids having to pass around this descriptor,
     and the speed hit will be irrelevant anyway compared to video decoding/encoding.
     that's fine, because we don't need to keep state across subtitles. */
  cd = iconv_open("UTF-8", encoding);
  if (cd != (iconv_t)-1) {
    /* iconv doesn't seem to have a mode to do a dummy convert to just return the number
       of bytes needed, so we just allocate 6 times the number of bytes in the string,
       which should be the max we need for UTF-8 */
    size_t insz=strlen(text)+1;
    size_t outsz = insz*6;
    char *inptr = text, *outptr;
    newtext = (char*)malloc(outsz);
    if (!newtext) {
      warn(frontend, NULL, 0, "Memory allocation failed - cannot convert text\n");
      iconv_close(cd);
      return NULL;
    }
    outptr=newtext;
    if (iconv(cd, &inptr, &insz, &outptr, &outsz) < 0) {
      warn(frontend, NULL, 0, "Failed to convert text to UTF-8\n");
      free(newtext);
      newtext = NULL;
    }
    iconv_close(cd);
  }

#else
  if (!strcasecmp(encoding, "iso-8859-1") || !strcasecmp(encoding, "latin1")) {
      /* simple, characters above 0x7f are broken in two,
         and code points map to the iso-8859-1 8 bit codes */
      nbytes=0;
      for (ptr=text;*ptr;++ptr) {
        nbytes++;
        if (0x80&*(unsigned char*)ptr) nbytes++;
      }
      newtext=(char*)malloc(1+nbytes);
      if (!newtext) {
        warn(frontend, NULL, 0, "Memory allocation failed - cannot convert text");
        return NULL;
      }
      nbytes=0;
      for (ptr=text;*ptr;++ptr) {
        if (0x80&*(unsigned char*)ptr) {
          newtext[nbytes++]=0xc0|((*(unsigned char*)ptr)>>6);
          newtext[nbytes++]=0x80|((*(unsigned char*)ptr)&0x3f);
        }
        else {
          newtext[nbytes++]=*ptr;
        }
      }
      newtext[nbytes++]=0;
  }
#endif
  else {
      warn(frontend, NULL, 0, "encoding %d not handled in conversion!", encoding);
      newtext = strdup("");
  }
  return newtext;
}

static void remove_last_newline(char *text)
{
  if (*text) {
    char *ptr = text+strlen(text)-1;
    if (*ptr=='\n') *ptr=0;
  }
}

static int store_subtitle(ff2theora_kate_stream *this,
                          char *text, double t0, double t1, int x1, int x2, int y1, int y2,
                          int ignore_non_utf8,int *warned, unsigned int line, FILE *frontend)
{
  char *utf8;
  size_t len;
  int ret;

  remove_last_newline(text);

  /* we want all text to be UTF-8 */
  utf8=convert_subtitle_to_utf8(this->subtitles_encoding,text,ignore_non_utf8, frontend);
  if (!utf8) {
    warn(frontend, this->filename, line, "Failed to get UTF-8 text");
    return -1;
  }

  len = strlen(utf8);
  this->subtitles = (ff2theora_subtitle*)realloc(this->subtitles, (this->num_subtitles+1)*sizeof(ff2theora_subtitle));
  if (!this->subtitles) {
    free(utf8);
    warn(frontend, NULL, 0, "Out of memory");
    return -1;
  }
  ret=kate_text_validate(kate_utf8,utf8,len+1);
  if (ret<0) {
    if (!*warned) {
      warn(frontend, this->filename, line, "subtitle is not valid UTF-8: %s",utf8);
      if (!frontend)
        fprintf(stderr,"  further invalid subtitles will NOT be flagged\n");
      *warned=1;
    }
  }
  else {
    /* kill off trailing \n characters */
    while (len>0) {
      if (utf8[len-1]=='\n') utf8[--len]=0; else break;
    }
    this->subtitles[this->num_subtitles].text = utf8;
    this->subtitles[this->num_subtitles].len = len;
    this->subtitles[this->num_subtitles].t0 = t0;
    this->subtitles[this->num_subtitles].t1 = t1;
    this->subtitles[this->num_subtitles].x1 = x1;
    this->subtitles[this->num_subtitles].x2 = x2;
    this->subtitles[this->num_subtitles].y1 = y1;
    this->subtitles[this->num_subtitles].y2 = y2;
    this->num_subtitles++;
  }

  return 0;
}

#endif

int load_subtitles(ff2theora_kate_stream *this, int ignore_non_utf8, FILE *frontend)
{
#ifdef HAVE_KATE
    enum { need_id, need_timing, need_text };
    int need = need_id;
    int last_seen_id=0;
    int ret;
    int id;
    static char text[4096];
    int h0,m0,s0,ms0,h1,m1,s1,ms1;
    int x1,x2,y1,y2;
    double t0=0.0;
    double t1=0.0;
    static char str[4096];
    int warned=0;
    FILE *f;
    unsigned int line=0;

    this->subtitles = NULL;

    if (!this->filename) {
        warn(frontend, NULL, 0, "No subtitles file to load from");
        return -1;
    }

    f = fopen(this->filename, "r");
    if (!f) {
        warn(frontend, NULL, 0, "Failed to open subtitles file %s (%s)", this->filename, strerror(errno));
        return -1;
    }

    /* first, check for a BOM */
    ret=fread(str,1,3,f);
    if (ret<3 || memcmp(str,"\xef\xbb\xbf",3)) {
      /* No BOM, rewind */
      fseek(f,0,SEEK_SET);
    }

    fgets2(str,sizeof(str),f);
    ++line;
    while (!feof(f) || *str) {
      switch (need) {
        case need_id:
          if (!strcmp(str,"\n")) {
            /* be nice and ignore extra empty lines between records */
          }
          else {
            ret=sscanf(str,"%d\n",&id);
            if (ret!=1 || id<0) {
              warn(frontend, this->filename, line, "Syntax error: %s",str);
              fclose(f);
              free(this->subtitles);
              return -1;
            }
            if (id!=last_seen_id+1) {
              warn(frontend, this->filename, line, "Non consecutive ids: %s - pretending not to have noticed",str);
            }
            last_seen_id=id;
            need=need_timing;
            strcpy(text,"");
          }
          break;
        case need_timing:
          ret=sscanf(str,"%d:%d:%d%*[.,]%d --> %d:%d:%d%*[.,]%d %*[xX]1: %d %*[xX]2: %d %*[yY]1: %d %*[yY]2: %d\n",&h0,&m0,&s0,&ms0,&h1,&m1,&s1,&ms1,&x1,&x2,&y1,&y2);
          if (ret!=12) {
            x1=y1=x2=y2=-INT_MAX;
            ret=sscanf(str,"%d:%d:%d%*[.,]%d --> %d:%d:%d%*[.,]%d\n",&h0,&m0,&s0,&ms0,&h1,&m1,&s1,&ms1);
            if (ret!=8) {
              warn(frontend, this->filename, line, "Syntax error: %s",str);
              fclose(f);
              free(this->subtitles);
              return -1;
            }
          }
          t0=hmsms2s(h0,m0,s0,ms0);
          t1=hmsms2s(h1,m1,s1,ms1);
          if ((h0|m0|s0|ms0)<0 || (h1|m1|s1|ms1)<0) {
            warn(frontend, this->filename, line, "Bad timestamp specification: %s",str);
            fclose(f);
            free(this->subtitles);
            return -1;
          }
          need=need_text;
          break;
        case need_text:
          if (str[0]=='\n') {
            /* we have all the lines for that subtitle, remove the last \n */
            int ret = store_subtitle(this, text, t0, t1, x1, x2, y1, y2, ignore_non_utf8, &warned, line, frontend);
            if (ret < 0) {
              fclose(f);
              free(this->subtitles);
              return ret;
            }
            need=need_id;
          }
          else {
            /* in case of very long subtitles */
            size_t len=strlen(text);
            if (len+strlen(str) >= sizeof(text)) {
              warn(frontend, this->filename, line, "Subtitle text is too long - truncated");
            }
            strncpy(text+len,str,sizeof(text)-len);
            text[sizeof(text)-1]=0;
          }
          break;
      }
      fgets2(str,sizeof(str),f);
      ++line;
    }

    fclose(f);

    if (need!=need_id) {
      /* shouldn't be a problem though, but warn */
      warn(frontend, this->filename, line, "Missing data in - truncated file ?");

      /* add any leftover text we've accumulated */
      if (need == need_text && text[0]) {
        int ret = store_subtitle(this, text, t0, t1, x1, x2, y1, y2, ignore_non_utf8, &warned, line, frontend);
        if (ret < 0) {
          fclose(f);
          free(this->subtitles);
          return ret;
        }
      }
    }

    /* fprintf(stderr,"  %u subtitles loaded.\n", this->num_subtitles); */

    return this->num_subtitles;
#else
    return 0;
#endif
}

int add_subtitle_for_stream(ff2theora_kate_stream *streams, int nstreams, int idx, float t, float duration, const char *utf8, size_t utf8len, FILE *frontend)
{
#ifdef HAVE_KATE
  int n, ret;
  for (n=0; n<nstreams; ++n) {
    ff2theora_kate_stream *ks=streams+n;
    if (idx == ks->stream_index) {
      ks->subtitles = (ff2theora_subtitle*)realloc(ks->subtitles, (ks->num_subtitles+1)*sizeof(ff2theora_subtitle));
      if (!ks->subtitles) {
        warn(frontend, NULL, 0, "Out of memory");
        return -1;
      }
      ret=kate_text_validate(kate_utf8,utf8,utf8len);
      if (ret<0) {
        warn(frontend, NULL, 0, "stream %d: subtitle %s is not valid UTF-8",idx,utf8);
      }
      else {
        /* make a copy */
        size_t len = utf8len;
        char *utf8copy = (char*)malloc(utf8len);
        if (!utf8copy) {
	  warn(frontend, NULL, 0, "Out of memory");
	  return -1;
        }
        memcpy(utf8copy, utf8, utf8len);
        /* kill off trailing \n characters */
        while (len>0) {
	  if (utf8copy[len-1]=='\n') utf8copy[--len]=0; else break;
        }
        ks->subtitles[ks->num_subtitles].text = utf8copy;
        ks->subtitles[ks->num_subtitles].len = utf8len;
        ks->subtitles[ks->num_subtitles].t0 = t;
        ks->subtitles[ks->num_subtitles].t1 = t+duration;
        ks->subtitles[ks->num_subtitles].x1 = -INT_MAX;
        ks->subtitles[ks->num_subtitles].x2 = -INT_MAX;
        ks->subtitles[ks->num_subtitles].y1 = -INT_MAX;
        ks->subtitles[ks->num_subtitles].y2 = -INT_MAX;
        ks->num_subtitles++;
      }
    }
  }
#endif
  return 0;
}

int add_image_subtitle_for_stream(ff2theora_kate_stream *streams, int nstreams, int idx, float t, float duration, const AVSubtitleRect *sr, int org_width, int org_height, FILE *frontend)
{
#ifdef HAVE_KATE
  int n, c, ret;
  ff2theora_subtitle *subtitle;

  if (sr->nb_colors <= 0 || sr->nb_colors > 256) {
    warn(frontend, NULL, 0, "Unsupported number of colors in image subtitle: %d", sr->nb_colors);
    return -1;
  }

  for (n=0; n<nstreams; ++n) {
    ff2theora_kate_stream *ks=streams+n;
    if (idx == ks->stream_index) {
      ks->subtitles = (ff2theora_subtitle*)realloc(ks->subtitles, (ks->num_subtitles+1)*sizeof(ff2theora_subtitle));
      if (!ks->subtitles) {
        warn(frontend, NULL, 0, "Out of memory");
        return -1;
      }
      subtitle = &ks->subtitles[ks->num_subtitles];

      kate_region_init(&subtitle->kr);
      subtitle->kr.metric = kate_millionths;
      subtitle->kr.x = 1000000 * sr->x / org_width;
      subtitle->kr.y = 1000000 * sr->y / org_height;
      subtitle->kr.w = 1000000 * sr->w / org_width;
      subtitle->kr.h = 1000000 * sr->h / org_height;

      kate_palette_init(&subtitle->kp);
      subtitle->kp.ncolors = sr->nb_colors;
      subtitle->kp.colors = malloc(sizeof(kate_color) * subtitle->kp.ncolors);
      if (!subtitle->kp.colors) {
        warn(frontend, NULL, 0, "Out of memory");
        return -1;
      }
      const uint32_t *pal = (const uint32_t*)sr->pict.data[1];
      for (c=0; c<subtitle->kp.ncolors; ++c) {
        subtitle->kp.colors[c].a = (pal[c]>>24)&0xff;
        subtitle->kp.colors[c].r = (pal[c]>>16)&0xff;
        subtitle->kp.colors[c].g = (pal[c]>>8)&0xff;
        subtitle->kp.colors[c].b = pal[c]&0xff;
      }

      kate_bitmap_init(&subtitle->kb);
      subtitle->kb.type = kate_bitmap_type_paletted;
      subtitle->kb.width = sr->w;
      subtitle->kb.height = sr->h;
      subtitle->kb.bpp = 0;
      while ((1<<subtitle->kb.bpp) < sr->nb_colors) ++subtitle->kb.bpp;
      subtitle->kb.pixels = malloc(sr->w*sr->h);
      if (!subtitle->kb.pixels) {
        free(subtitle->kp.colors);
        warn(frontend, NULL, 0, "Out of memory");
        return -1;
      }

      /* Not quite sure if the AVPicture line data is supposed to always be packed */
      memcpy(subtitle->kb.pixels,sr->pict.data[0],sr->w*sr->h);

      subtitle->text = NULL;
      subtitle->t0 = t;
      subtitle->t1 = t+duration;
      ks->num_subtitles++;
    }
  }
#endif
  return 0;
}

void free_subtitles(ff2theora this)
{
    size_t i,n;
    for (i=0; i<this->n_kate_streams; ++i) {
        ff2theora_kate_stream *ks=this->kate_streams+i;
        for (n=0; n<ks->num_subtitles; ++n) free(ks->subtitles[n].text);
        free(ks->subtitles);
        free(ks->subtitles_encoding);
    }
    free(this->kate_streams);
}

