#!/bin/sh

#optional, if you have those libs installed:
#extra="--enable-version3 --enable-libopencore-amrnb --enable-libopencore-amrwb"

#apt-get install liba52-dev libfaad-dev libgsm1-dev
#extra="$extra --enable-libfaad --enable-libgsm"

common="--enable-version3 --enable-gpl --enable-postproc --disable-muxers --disable-encoders --enable-libvorbis"
common="$common --disable-ffmpeg --disable-ffplay --disable-ffserver --disable-doc"

#linux
options="$common --enable-pthreads $extra"

#mingw32
uname | grep MINGW && options="$common --enable-memalign-hack --enable-mingw32 --extra-cflags=-I/usr/local/include --extra-ldflags=-L/usr/local/lib $extra"

# load FFMPEG specific properties
. ./ffmpegrev

#Get ffmpeg from svn
svn -r $FFMPEG_REVISION co $FFMPEG_SVN $FFMPEG_CO_DIR
svn update -r $FFMPEG_EXTERNALS_REVISION $FFMPEG_CO_DIR/libswscale

apply_patches() {
  cd ffmpeg
  for patch in ../patches/*.patch; do
    patch -p0 < $patch
  done
  touch .ffmpeg2theora_patched
  cd ..
}

test -e ffmpeg/.ffmpeg2theora_patched || apply_patches
#configure and build ffmpeg
cd ffmpeg && ./configure $options && make

