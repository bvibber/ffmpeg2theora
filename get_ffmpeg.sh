#!/bin/sh

common="--enable-version3 --enable-gpl --enable-postproc --disable-muxers --disable-encoders --enable-libvorbis"
common="$common --disable-ffmpeg --disable-ffplay --disable-ffserver --disable-ffprobe --disable-doc"

#optional, if you have those libs installed:
#extra="$extra --enable-libopencore-amrnb --enable-libopencore-amrwb"

#apt-get install liba52-dev libgsm1-dev
#extra="$extra  --enable-libgsm"

#optional, if you have libvpx installed:
#extra="$extra --enable-libvpx"

#linux
options="$common --enable-pthreads $extra"

#mingw32
uname | grep MINGW && options="$common --enable-memalign-hack --enable-mingw32 --extra-cflags=-I/usr/local/include --extra-ldflags=-L/usr/local/lib $extra"

# load FFMPEG specific properties
. ./ffmpegrev

test -e $FFMPEG_CO_DIR || git clone $FFMPEG_URL $FFMPEG_CO_DIR
cd $FFMPEG_CO_DIR
#git pull -r $FFMPEG_REVISION
git checkout release/0.7 
git pull
cd ..

apply_patches() {
  cd $FFMPEG_CO_DIR
  for patch in ../patches/*.patch; do
    patch -p0 < $patch
  done
  touch .ffmpeg2theora_patched
  cd ..
}

#test -e $FFMPEG_CO_DIR/.ffmpeg2theora_patched || apply_patches
#configure and build ffmpeg
cd $FFMPEG_CO_DIR && ./configure $options && make

