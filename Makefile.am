AUTOMAKE_OPTIONS = 1.6 dist-zip

SUBDIRS = kino_export

EXTRA_DIST = Makefile.mingw32

EXTRA="-D_FILE_OFFSET_BITS=64"

bin_PROGRAMS = ffmpeg2theora

ffmpeg2theora_SOURCES = ffmpeg2theora.c  theorautils.c
ffmpeg2theora_LDFLAGS = -static @FFMPEG_LIBS@ @XIPH_LIBS@
ffmpeg2theora_CFLAGS = $(EXTRA) @XIPH_CFLAGS@ @FFMPEG_CFLAGS@
