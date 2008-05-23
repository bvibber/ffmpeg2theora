AUTOMAKE_OPTIONS = 1.8 dist-bzip2 no-dist-gzip

SUBDIRS = kino_export

EXTRA_DIST = Makefile get_ffmpeg_svn.sh \
             ffmpeg2theora.1 \
             get_libkate.sh subtitles.txt

bin_PROGRAMS = ffmpeg2theora

ffmpeg2theora_SOURCES = src/ffmpeg2theora.c src/theorautils.c src/subtitles.c \
                        src/ffmpeg2theora.h src/theorautils.h src/subtitles.h
ffmpeg2theora_LDFLAGS = -L$(prefix)/lib @XIPH_LIBS@ @KATE_LIBS@ @FFMPEG_LIBS@
ffmpeg2theora_CFLAGS = -I./src @XIPH_CFLAGS@ @KATE_CFLAGS@ @FFMPEG_CFLAGS@

man_MANS = ffmpeg2theora.1
