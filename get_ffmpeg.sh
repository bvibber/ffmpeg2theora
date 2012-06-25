#!/bin/sh
cd `dirname $0`

# load FFMPEG specific properties
. ./ffmpegrev

test -e $FFMPEG_CO_DIR || git clone --depth 1 $FFMPEG_URL $FFMPEG_CO_DIR
cd $FFMPEG_CO_DIR
#git pull -r $FFMPEG_REVISION
#git checkout release/0.7 
git checkout master
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

./build_ffmpeg.sh

