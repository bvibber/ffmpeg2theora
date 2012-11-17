#!/bin/sh
. ./ffmpegrev


common="--enable-gpl --enable-postproc --disable-muxers --disable-encoders --enable-libvorbis"
common="$common --disable-ffmpeg --disable-ffplay --disable-ffserver --disable-ffprobe --disable-doc"

#optional, if you have those libs installed(requires GPL3):
#extra="$extra --enable-version3 --enable-libopencore-amrnb --enable-libopencore-amrwb"

#apt-get install liba52-dev libgsm1-dev
#extra="$extra  --enable-libgsm"

#optional, if you have libvpx installed:
#extra="$extra --enable-libvpx"

#linux
options="$common --enable-pthreads $extra"

#mingw32
uname | grep MINGW && options="$common --enable-memalign-hack --enable-mingw32 --extra-cflags=-I/usr/local/include --extra-ldflags=-L/usr/local/lib $extra"

#configure and build ffmpeg
cd $FFMPEG_CO_DIR && ./configure $options && make -j8

