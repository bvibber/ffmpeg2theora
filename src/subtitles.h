#ifndef _F2T_SUBTITLES_H_
#define _F2T_SUBTITLES_H_

#ifdef HAVE_KATE
#include "kate/kate.h"
#endif
#include "ffmpeg2theora.h"

#ifndef __GNUC__
/* Windows doesn't have strcasecmp but stricmp (at least, DOS had)
   (or was that strcmpi ? Might have been Borland C) */
#define strcasecmp(s1, s2) stricmp(s1, s2)
#endif


#define SUPPORTED_ENCODINGS "utf-8, utf8, iso-8859-1, latin1"

extern void add_kate_stream(ff2theora this);
extern int load_subtitles(ff2theora_kate_stream *this);
extern void free_subtitles(ff2theora this);

extern void set_subtitles_file(ff2theora this,const char *filename);
extern void set_subtitles_language(ff2theora this,const char *language);
extern void set_subtitles_category(ff2theora this,const char *category);
extern void set_subtitles_encoding(ff2theora this,F2T_ENCODING encoding);
extern void report_unknown_subtitle_encoding(const char *name);

extern char *fgets2(char *s,size_t sz,FILE *f);
extern double hmsms2s(int h,int m,int s,int ms);
extern void convert_subtitle_to_utf8(F2T_ENCODING encoding,unsigned char *text);
#endif

