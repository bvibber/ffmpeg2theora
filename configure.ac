dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)
AC_INIT(ffmpeg2theora,0.10.1)
AC_CONFIG_SRCDIR([Makefile.am])
AM_INIT_AUTOMAKE([dist-bzip2])

AM_DISABLE_SHARED
AM_ENABLE_STATIC

AC_PROG_CC
AM_PROG_LIBTOOL

dnl ffmpeg integration here
AC_ARG_WITH(ffmpegprefix,
       AS_HELP_STRING([--with-ffmpegprefix],
	[prefix to ffmpeg source, if libavcodec and libavformat are not in . (default=.)]),
       ffmpegprefix="$withval", ffmpegprefix="ffmpeg")

dnl only check for libavcodec.a and libformat.a if not crosscompiling
dnl
if test "$cross_compiling" = yes; then
  HAVE_FFMPEG=yes
else
  AC_CHECK_FILES([$ffmpegprefix $ffmpegprefix/libavcodec/libavcodec.a $ffmpegprefix/libavformat/libavformat.a],[HAVE_FFMPEG=yes])
fi

dnl if not using ffmpegprefix try to look for shared files
if test "x$HAVE_FFMPEG" != xyes; then
	AC_CHECK_HEADER(ffmpeg/avcodec.h,[HAVE_SHARED_AVCODEC=yes])
	AC_CHECK_HEADER(ffmpeg/avformat.h, [HAVE_SHARED_AVFORMAT=yes])
	AC_CHECK_FILE(/usr/local/include/ffmpeg, [HAVE_FFMPEG_LOCAL=yes])
	if test "x$HAVE_SHARED_AVCODEC$HAVE_SHARED_AVCODEC" != xyesyes; then
		AC_MSG_ERROR([
	you need libavcodec and libavformat from ffmpeg cvs
	to get it run get_ffmpeg_cvs.sh or point configure to
	your local copy with i.e.
	./configure --with-ffmpegprefix=/usr/local/src/ffmpeg
	(see INSTALL for more information)
	])
	else
		if test "x$HAVE_FFMPEG_LOCAL" != xyes; then
			FFMPEG_CFLAGS="-I/usr/include/ffmpeg"
		else
			FFMPEG_CFLAGS="-I/usr/local/include/ffmpeg"
		fi
		FFMPEG_EXTLIBS="-lm -lz"
		FFMPEG_LIBS="-L/usr/local/lib -L/usr/lib -lavformat -lavcodec $FFMPEG_EXTLIBS"
	fi
	
else
	FFMPEG_CFLAGS="-I$ffmpegprefix/libavformat -I$ffmpegprefix/libavcodec"
	FFMPEG_EXTLIBS="-lm -lz"
	FFMPEG_LIBS="$ffmpegprefix/libavformat/libavformat.a $ffmpegprefix/libavcodec/libavcodec.a $FFMPEG_EXTLIBS"
fi


AC_CHECK_LIB(faad,faacDecClose, [
  AC_CHECK_HEADER(faad.h, [HAVE_FAAD=yes])
    ],,[-lm -lz])
if test "x$HAVE_FAAD" = xyes; then
	FFMPEG_EXTLIBS="-lfaad"
	FFMPEG_LIBS="$FFMPEG_LIBS $FFMPEG_EXTLIBS"
fi
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

PKG_CHECK_MODULES(XIPH,ogg >= 1.1 vorbis vorbisenc theora )

AC_SUBST(XIPH_CFLAGS)
AC_SUBST(XIPH_LIBS)
AC_OUTPUT([
	Makefile
	kino_export/Makefile
])
