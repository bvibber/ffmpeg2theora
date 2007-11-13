dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)
AC_INIT(ffmpeg2theora,0.19)
AC_CONFIG_SRCDIR([Makefile.am])
AM_INIT_AUTOMAKE([dist-bzip2])

AC_PROG_CC
AM_PROG_LIBTOOL

AC_SYS_LARGEFILE

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

PKG_CHECK_MODULES(XIPH,ogg >= 1.1 vorbis vorbisenc theora )

AC_SUBST(XIPH_CFLAGS)
AC_SUBST(XIPH_LIBS)

PKG_CONFIG_PATH=./ffmpeg:$PKG_CONFIG_PATH
PKG_CHECK_MODULES(FFMPEG, libavformat libavcodec libswscale libpostproc, HAVE_FFMPEG=yes, 
        AC_MSG_ERROR([

could not find libavformat libavcodec libswscale libpostproc.
install it 
 sudo apt-get install libavformat-dev libavcodec-dev libswscale-dev libpostproc-dev
or update PKG_CONFIG_PATH to point to ffmpegs source folder
or run ./get_ffmpeg_svn.sh (for more information see INSTALL)
])
)
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

AC_OUTPUT([
	Makefile
	kino_export/Makefile
])
