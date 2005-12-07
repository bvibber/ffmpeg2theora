#!/bin/sh

#optional, if you have the libs installed:
#extra="--enable-faad --enable-dts"

cvs -z3 -d:pserver:anonymous@cvs.mplayerhq.hu:/cvsroot/ffmpeg/ co ffmpeg
cd ffmpeg
./configure --enable-libogg --enable-theora --enable-a52 --enable-pthreads --enable-gpl --disable-encoders  $extra
make 
