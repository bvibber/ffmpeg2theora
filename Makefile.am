AUTOMAKE_OPTIONS = 1.6 dist-bzip2 no-dist-gzip

SUBDIRS = kino_export

EXTRA_DIST = Makefile.mingw32 theorautils.h get_ffmpeg_cvs.sh

EXTRA=-D_FILE_OFFSET_BITS=64 -I$(prefix)/include

bin_PROGRAMS = ffmpeg2theora

ffmpeg2theora_SOURCES = ffmpeg2theora.c  theorautils.c
ffmpeg2theora_LDFLAGS = -static -L$(prefix)/lib @FFMPEG_LIBS@ @XIPH_LIBS@
ffmpeg2theora_CFLAGS = $(EXTRA) @XIPH_CFLAGS@ @FFMPEG_CFLAGS@
