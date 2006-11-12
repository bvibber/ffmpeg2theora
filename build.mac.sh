#!/bin/bash
#build universal binary of ffmpeg2theora and package it
#

build_all_osx() {
  unset PKG_CONFIG_PATH
  
  tmp_dir=/tmp/ffmpeg2theora
  build_dir=$tmp_dir/$arch
  dist_dir=$tmp_dir/dist/$arch
  mkdir -p $build_dir
  mkdir -p $dist_dir

  ffmpeg_extra="--enable-pp --enable-gpl --enable-swscaler --disable-encoders"
  export CFLAGS="-arch $arch -isysroot /Developer/SDKs/MacOSX10.4u.sdk"
  export LDFLAGS="-arch $arch -isysroot /Developer/SDKs/MacOSX10.4u.sdk"
  
  echo "building $arch/libogg"
  cd $build_dir
  test -e ogg || svn co https://svn.xiph.org/trunk/ogg
  cd ogg && svn up &&  ./autogen.sh && make && cd ..
  export PKG_CONFIG_PATH=$build_dir/ogg:$PKG_CONFIG_PATH

  echo "building libvorbis-aotuv"
  cd $build_dir
  test -e vorbis-aotuv || svn co https://svn.xiph.org/branches/vorbis-aotuv/
  cd vorbis-aotuv && svn up && ./autogen.sh && make
  export PKG_CONFIG_PATH=$build_dir/vorbis-aotuv:$PKG_CONFIG_PATH

  echo "building $arch/libtheora"
  cd $build_dir
  test -e theora || svn co https://svn.xiph.org/trunk/theora
  cd theora && ./autogen.sh && make
  export PKG_CONFIG_PATH=$build_dir/theora:$PKG_CONFIG_PATH

  echo "building $arch/ffmpeg"
  cd $build_dir
  
  test -e ffmpeg || svn co svn://svn.mplayerhq.hu/ffmpeg/trunk ffmpeg
  cd ffmpeg
  #svn up
  unset CFLAGS
  unset LDFLAGS
  ./configure --cross-compile $ffmpeg_extra --arch=$arch  --extra-ldflags='-L$dist_dir/lib -arch $arch -isysroot /Developer/SDKs/MacOSX10.4u.sdk' --extra-cflags='-I$dist_dir/include -arch $arch -isysroot /Developer/SDKs/MacOSX10.4u.sdk'
  make
  export PKG_CONFIG_PATH=$build_dir/ffmpeg:$PKG_CONFIG_PATH

  echo "build $arch/ffmpeg2theora"
  export CFLAGS="-arch $arch -isysroot /Developer/SDKs/MacOSX10.4u.sdk"
  export LDFLAGS="-arch $arch -isysroot /Developer/SDKs/MacOSX10.4u.sdk"  
  cd $build_dir
  test -e ffmpeg2theora || svn co https://svn.xiph.org/trunk/ffmpeg2theora
  cd ffmpeg2theora &&  svn up && ./autogen.sh --prefix=$dist_dir --with-static-linking
  make && make install
  
}

arch=ppc build_all_osx
arch=i386 build_all_osx

echo "building universal/ffmpeg2theora"
tmp_dir=/tmp/ffmpeg2theora
build_dir=$tmp_dir/build
dist_dir=$tmp_dir/dist
mkdir -p $dist_dir/universal/usr/local/bin
strip $dist_dir/ppc/bin/ffmpeg2theora
strip $dist_dir/i386/bin/ffmpeg2theora
lipo -create -arch ppc $dist_dir/ppc/bin/ffmpeg2theora \
     -arch i386 $dist_dir/i386/bin/ffmpeg2theora \
     -output $dist_dir/universal/usr/local/bin/ffmpeg2theora

version=`grep AC_INIT  $dist_dir/../i386/ffmpeg2theora/configure.ac | cut -f 2 -d"," | cut -f 1 -d")"`
sudo /Developer/Applications/Utilities/PackageMaker.app/Contents/MacOS/PackageMaker \
  -build -proj /tmp/ffmpeg2theora/i386/ffmpeg2theora/ffmpeg2theora.pmproj \
  -p $dist_dir/ffmpeg2theora-$version.pkg

cd $dist_dir
zip -r ffmpeg2theora-$version.pkg.zip ffmpeg2theora-$version.pkg

