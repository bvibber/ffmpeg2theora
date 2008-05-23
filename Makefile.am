AUTOMAKE_OPTIONS = 1.8 dist-bzip2 no-dist-gzip

SUBDIRS = kino_export

EXTRA_DIST = Makefile.mingw32 ffmpeg2theora.h theorautils.h get_ffmpeg_svn.sh ffmpeg2theora.1 \
             get_libkate.sh subtitles.h subtitles.txt

bin_PROGRAMS = ffmpeg2theora

ffmpeg2theora_SOURCES = ffmpeg2theora.c theorautils.c subtitles.c
ffmpeg2theora_LDFLAGS = -L$(prefix)/lib @XIPH_LIBS@ @KATE_LIBS@ @FFMPEG_LIBS@
ffmpeg2theora_CFLAGS = @XIPH_CFLAGS@ @KATE_CFLAGS@ @FFMPEG_CFLAGS@

man_MANS = ffmpeg2theora.1
