/* -*- tab-width:4;c-file-style:"cc-mode"; -*- */
/*
 * subtitles.c -- Kate Subtitles
 * Copyright (C) 2007-2008 <j@v2v.cc>
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

#include "libavformat/avformat.h"

#ifdef WIN32
#include "fcntl.h"
#endif

#include "theorautils.h"
#include "subtitles.h"


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
    ks->subtitles_count = 0; /* denotes not set yet */
    ks->subtitles_encoding = ENC_UNSET;
    strcpy(ks->subtitles_language, "");
    strcpy(ks->subtitles_category, "");
}

/*
 * sets the filename of the next subtitles file
 */
void set_subtitles_file(ff2theora this,const char *filename){
  size_t n;
  for (n=0; n<this->n_kate_streams;++n) {
    if (!this->kate_streams[n].filename) break;
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
    if (!this->kate_streams[n].subtitles_language[0]) break;
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
    if (!this->kate_streams[n].subtitles_category[0]) break;
  }
  if (n==this->n_kate_streams) add_kate_stream(this);
  strncpy(this->kate_streams[n].subtitles_category, category, 16);
  this->kate_streams[n].subtitles_category[15] = 0;
}

/**
  * sets the encoding of the next subtitles file
  */
void set_subtitles_encoding(ff2theora this,F2T_ENCODING encoding){
  size_t n;
  for (n=0; n<this->n_kate_streams;++n) {
    if (this->kate_streams[n].subtitles_encoding==ENC_UNSET) break;
  }
  if (n==this->n_kate_streams) add_kate_stream(this);
  this->kate_streams[n].subtitles_encoding = encoding;
}


void report_unknown_subtitle_encoding(const char *name)
{
  fprintf(stderr, "Unknown character encoding: %s\n",name);
  fprintf(stderr, "Valid character encodings are:\n");
  fprintf(stderr, "  " SUPPORTED_ENCODINGS "\n");
}

#ifdef HAVE_KATE

static char *fgets2(char *s,size_t sz,FILE *f)
{
    char *ret = fgets(s, sz, f);
    /* fixup DOS newline character */
    char *ptr=strchr(s, '\r');
    if (ptr) {
      *ptr='\n';
      *(ptr+1)=0;
    }
    return ret;
}

static double hmsms2s(int h,int m,int s,int ms)
{
    return h*3600+m*60+s+ms/1000.0;
}

/* very simple implementation when no iconv */
static char *convert_subtitle_to_utf8(F2T_ENCODING encoding,char *text,int ignore_non_utf8)
{
  size_t nbytes;
  char *ptr;
  char *newtext = NULL;
  int errors=0;

  if (!text) return NULL;

  switch (encoding) {
    case ENC_UNSET:
      /* we don't know what encoding this is, assume utf-8 and we'll yell if it ain't */
      /* fall through */
    case ENC_UTF8:
      /* nothing to do, already in utf-8 */
      if (ignore_non_utf8) {
        /* actually, give the user the option of just ignoring non UTF8 characters */
        char *wptr;
        size_t wlen0;

        nbytes = strlen(text)+1;
        newtext=(char*)malloc(nbytes);
        if (!newtext) {
          fprintf(stderr, "WARNING - Memory allocation failed - cannot convert text\n");
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
              fprintf(stderr, "WARNING - failed to filter utf8 text: %s\n", text);
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
          fprintf(stderr, "WARNING - Found non utf8 character(s) in string %s, scrubbed out\n", text);
        }
      }
      else {
        newtext = strdup(text);
      }
      break;
    case ENC_ISO_8859_1:
      /* simple, characters above 0x7f are broken in two,
         and code points map to the iso-8859-1 8 bit codes */
      nbytes=0;
      for (ptr=text;*ptr;++ptr) {
        nbytes++;
        if (0x80&*(unsigned char*)ptr) nbytes++;
      }
      newtext=(char*)malloc(1+nbytes);
      if (!newtext) {
        fprintf(stderr, "WARNING - Memory allocation failed - cannot convert text\n");
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
      break;
    default:
      fprintf(stderr, "ERROR: encoding %d not handled in conversion!\n", encoding);
      newtext = strdup("");
      break;
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

#endif

int load_subtitles(ff2theora_kate_stream *this, int ignore_non_utf8)
{
#ifdef HAVE_KATE
    enum { need_id, need_timing, need_text };
    int need = need_id;
    int last_seen_id=0;
    int ret;
    int id;
    static char text[4096];
    int h0,m0,s0,ms0,h1,m1,s1,ms1;
    double t0=0.0;
    double t1=0.0;
    static char str[4096];
    int warned=0;
    FILE *f;
    size_t len;
    unsigned int line=0;
    char *utf8;

    this->subtitles = NULL;

    if (!this->filename) {
        fprintf(stderr,"WARNING - No subtitles file to load from\n");
        return -1;
    }

    f = fopen(this->filename, "r");
    if (!f) {
        fprintf(stderr,"WARNING - Failed to open subtitles file %s (%s)\n", this->filename, strerror(errno));
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
    while (!feof(f)) {
      switch (need) {
        case need_id:
          if (!strcmp(str,"\n")) {
            /* be nice and ignore extra empty lines between records */
          }
          else {
            ret=sscanf(str,"%d\n",&id);
            if (ret!=1 || id<0) {
              fprintf(stderr,"WARNING - %s:%u: Syntax error: %s\n",this->filename,line,str);
              fclose(f);
              free(this->subtitles);
              return -1;
            }
            if (id!=last_seen_id+1) {
              fprintf(stderr,"WARNING - %s:%u: non consecutive ids: %s - pretending not to have noticed\n",this->filename,line,str);
            }
            last_seen_id=id;
            need=need_timing;
            strcpy(text,"");
          }
          break;
        case need_timing:
          ret=sscanf(str,"%d:%d:%d%*[.,]%d --> %d:%d:%d%*[.,]%d\n",&h0,&m0,&s0,&ms0,&h1,&m1,&s1,&ms1);
          if (ret!=8 || (h0|m0|s0|ms0)<0 || (h1|m1|s1|ms1)<0) {
            fprintf(stderr,"WARNING - %s:%u: Syntax error: %s\n",this->filename,line,str);
            fclose(f);
            free(this->subtitles);
            return -1;
          }
          else {
            t0=hmsms2s(h0,m0,s0,ms0);
            t1=hmsms2s(h1,m1,s1,ms1);
          }
          need=need_text;
          break;
        case need_text:
          if (str[0]=='\n') {
            /* we have all the lines for that subtitle, remove the last \n */
            remove_last_newline(text);

            /* we want all text to be UTF8 */
            utf8=convert_subtitle_to_utf8(this->subtitles_encoding,text,ignore_non_utf8);
            if (!utf8) {
              fclose(f);
              free(this->subtitles);
              return -1;
              break;
            }

            len = strlen(utf8);
            this->subtitles = (ff2theora_subtitle*)realloc(this->subtitles, (this->num_subtitles+1)*sizeof(ff2theora_subtitle));
            if (!this->subtitles) {
              free(utf8);
              fprintf(stderr, "Out of memory\n");
              fclose(f);
              free(this->subtitles);
              return -1;
            }
            ret=kate_text_validate(kate_utf8,utf8,len+1);
            if (ret<0) {
              if (!warned) {
                fprintf(stderr,"WARNING - %s:%u: subtitle %s is not valid utf-8\n",this->filename,line,utf8);
                fprintf(stderr,"  further invalid subtitles will NOT be flagged\n");
                warned=1;
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
              this->num_subtitles++;
            }
            need=need_id;
          }
          else {
            /* in case of very long subtitles */
            len=strlen(text);
            if (len+strlen(str) >= sizeof(text)) {
              fprintf(stderr,"WARNING - %s:%u: subtitle text is too long - truncated\n",this->filename,line);
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
      fprintf(stderr,"WARNING - %s:%u: missing data in %s - truncated file ?\n",this->filename,line,this->filename);
    }

    /* fprintf(stderr,"  %u subtitles loaded.\n", this->num_subtitles); */

    return this->num_subtitles;
#else
    return 0;
#endif
}

void free_subtitles(ff2theora this)
{
    size_t i,n;
    for (i=0; i<this->n_kate_streams; ++i) {
        ff2theora_kate_stream *ks=this->kate_streams+i;
        for (n=0; n<ks->num_subtitles; ++n) free(ks->subtitles[n].text);
        free(ks->subtitles);
    }
    free(this->kate_streams);
}

