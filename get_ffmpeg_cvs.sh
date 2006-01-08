#!/bin/sh

#optional, if you have the libs installed:
#extra="--enable-faad --enable-dts --enable-libgsm --enable-amr_nb --enable-amr_nb-fixed --enable-amr_wb --enable-amr_if2"

common="--enable-libogg --enable-theora --enable-a52  --enable-gpl --disable-encoders"

#linux
options="$common --enable-pthreads $extra"

#mingw32
uname | grep MINGW && options="$common --enable-memalign-hack --enable-mingw32 --extra-cflags=-I/usr/local/include --extra-ldflags=-L/usr/local/lib $extra"

cvs -z3 -d:pserver:anonymous@cvs.mplayerhq.hu:/cvsroot/ffmpeg co ffmpeg
cd ffmpeg && ./configure $options && make
