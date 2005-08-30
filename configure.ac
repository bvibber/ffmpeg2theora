dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)
AC_INIT(ffmpeg2theora,0.14.1)
AC_CONFIG_SRCDIR([Makefile.am])
AM_INIT_AUTOMAKE([dist-bzip2])

AC_PROG_CC
AM_PROG_LIBTOOL

AC_MSG_CHECKING(whether to link statically)
AC_ARG_WITH(static-linking,
[  --with-static-linking   Link binaries statically],
[ case "$withval" in
  no)
     AC_MSG_RESULT(no)
     ;;
  *)
    AC_MSG_RESULT(yes)
    LDFLAGS="-static $LDFLAGS"
    ;;
  esac],
AC_MSG_RESULT(no)
)

PKG_CHECK_MODULES(FFMPEG, libavformat libavcodec, HAVE_FFMPEG=yes, HAVE_FFMPEG=no)
if test x$HAVE_FFMPEG = xno; then
    export PKG_CONFIG_PATH=./ffmpeg:$PKG_CONFIG_PATH
    PKG_CHECK_MODULES(FFMPEG, libavformat libavcodec, HAVE_FFMPEG=yes, 
        AC_MSG_ERROR([

could not find ffmpeg. please update PKG_CONFIG_PATH to point to ffmpegs source
folder or run ./get_ffmpeg_cvs.sh (for more information see INSTALL)
])
)
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
