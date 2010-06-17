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

extern int is_valid_encoding(const char *encoding);
extern void add_kate_stream(ff2theora this);
extern int load_subtitles(ff2theora_kate_stream *this, int ignore_non_utf8, FILE *frontend);
extern void free_subtitles(ff2theora this);

extern void add_subtitles_stream(ff2theora this,int stream_index,const char *language,const char *category);
extern int add_subtitle_for_stream(ff2theora_kate_stream *streams, int nstreams, int idx, float t, float duration, const char *utf8, size_t utf8len, FILE *frontend);
extern int add_image_subtitle_for_stream(ff2theora_kate_stream *streams, int nstreams, int idx, float t, float duration, const AVSubtitleRect *sr, int org_width, int org_height, FILE *frontend);
extern void set_subtitles_file(ff2theora this,const char *filename);
extern void set_subtitles_language(ff2theora this,const char *language);
extern void set_subtitles_category(ff2theora this,const char *category);
extern void set_subtitles_encoding(ff2theora this,const char *encoding);
extern void report_unknown_subtitle_encoding(const char *name, FILE *frontend);

#endif

