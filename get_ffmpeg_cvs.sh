#!/bin/sh
cvs -z3 -d:pserver:anonymous@cvs.mplayerhq.hu:/cvsroot/ffmpeg/ co ffmpeg
cd ffmpeg
./configure --enable-libogg --enable-theora --disable-mp3lame --enable-a52 --enable-pthreads --enable-vhook --enable-dv1394 --enable-gpl
make 
