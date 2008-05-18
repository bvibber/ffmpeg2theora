#!/bin/sh

#optional, if you have those libs installed:
#apt-get install libamrnb-dev libamrwb-dev
#extra=" --enable-amr_nb --enable-amr_nb-fixed --enable-amr_wb --enable-amr_if2"

#apt-get install liba52-dev libfaad-dev libgsm1-dev
#extra=" --enable-liba52 --enable-libfaad --enable-libgsm"

common="--enable-gpl --enable-swscale --enable-postproc --disable-encoders --enable-libvorbis"

#linux
options="$common --enable-pthreads $extra"

#mingw32
uname | grep MINGW && options="$common --enable-memalign-hack --enable-mingw32 --extra-cflags=-I/usr/local/include --extra-ldflags=-L/usr/local/lib $extra"

svn co svn://svn.mplayerhq.hu/ffmpeg/trunk ffmpeg
cd ffmpeg && ./configure $options && make
