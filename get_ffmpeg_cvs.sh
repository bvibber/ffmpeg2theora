#!/bin/sh

#optional, if you have the libs installed:
#extra="--enable-faad --enable-dts --enable-libgsm --enable-amr_nb --enable-amr_nb-fixed --enable-amr_wb --enable-amr_if2"

cvs -z3 -d:pserver:anonymous@cvs.mplayerhq.hu:/cvsroot/ffmpeg co ffmpeg
cd ffmpeg && ./configure --enable-libogg --enable-theora --enable-a52 --enable-pthreads --enable-gpl --disable-encoders  --disable-muxers $extra && make
make 
