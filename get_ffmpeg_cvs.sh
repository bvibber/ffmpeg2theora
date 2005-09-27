#!/bin/sh
cvs -z3 -d:pserver:anonymous@cvs.mplayerhq.hu:/cvsroot/ffmpeg/ co ffmpeg
cd ffmpeg
./configure --enable-libogg --enable-theora --enable-a52 --enable-pthreads --enable-gpl
make 
