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

  ffmpeg_extra="--enable-pp --enable-gpl --enable-swscaler --disable-encoders  --enable-libogg --enable-vorbis"
  ffmpeg_extra="$ffmpeg_extra --enable-a52 --enable-dts"
  test -e $dist_dir/lib/libfaad.a && ffmpeg_extra="$ffmpeg_extra --enable-faad" && echo "building with faad"
  echo ""
  if [ $arch == 'ppc' ]; then
    ffmpeg_arch="powerpc"
    ffmpeg_extra="$ffmpeg_extra --cpu=G4"
  else
    ffmpeg_arch="x86_32"
    ffmpeg_extra="$ffmpeg_extra --cpu=pentium-m"
  fi
  export CFLAGS="-arch $arch"
  export LDFLAGS="-arch $arch"
  
  echo "building $arch/libogg"
  cd $build_dir
  test -e ogg || svn co https://svn.xiph.org/trunk/ogg
  cd ogg && svn up &&  ./autogen.sh --disable-shared --prefix=$dist_dir && make && make install 
  export PKG_CONFIG_PATH=$build_dir/ogg:$PKG_CONFIG_PATH

  echo "building libvorbis-aotuv"
  cd $build_dir
  test -e vorbis-aotuv || svn co https://svn.xiph.org/branches/vorbis-aotuv/
  cd vorbis-aotuv && svn up && ./autogen.sh --disable-shared --prefix=$dist_dir && make && make install
  export PKG_CONFIG_PATH=$build_dir/vorbis-aotuv:$PKG_CONFIG_PATH

  echo "building $arch/libtheora"
  cd $build_dir
  test -e theora || svn co https://svn.xiph.org/trunk/theora
  cd theora && ./autogen.sh --disable-shared --prefix=$dist_dir --host=$arch && make && make install
  export PKG_CONFIG_PATH=$build_dir/theora:$PKG_CONFIG_PATH
  
  echo "building $arch/libdts"
  cd $build_dir
  tarball="http://download.videolan.org/pub/videolan/libdca/0.0.2/libdca-0.0.2.tar.gz"
  test -e libdts-0.0.2 || curl $tarball > libdts-0.0.2.tar.gz && tar xzf libdts-0.0.2.tar.gz
  cd libdts-0.0.2
  ./configure --prefix=$dist_dir && make && make install
  export PKG_CONFIG_PATH=$dist_dir/lib/pkgconfig:$PKG_CONFIG_PATH
  
  echo "building $arch/ffmpeg"
  cd $build_dir
  test -e ffmpeg || svn co svn://svn.mplayerhq.hu/ffmpeg/trunk ffmpeg
  cd ffmpeg
  #svn up
  unset CFLAGS
  unset LDFLAGS
  ./configure --cross-compile $ffmpeg_extra --arch=$ffmpeg_arch  \
    --extra-ldflags="-L$dist_dir/lib -arch $arch" \
    --extra-cflags="-I$dist_dir/include -arch $arch"
  make

  export PKG_CONFIG_PATH=$build_dir/ffmpeg:$PKG_CONFIG_PATH
  echo $PKG_CONFIG_PATH
  echo "build $arch/ffmpeg2theora"
  export CFLAGS="-arch $arch"
  export LDFLAGS="-arch $arch"  
  cd $build_dir
  test -e ffmpeg2theora || svn co https://svn.xiph.org/trunk/ffmpeg2theora
  cd ffmpeg2theora &&  svn up && ./autogen.sh --prefix=$dist_dir --with-static-linking
  make && make install
  
}

echo "building ffmpeg2theora"
echo ""

arch=ppc build_all_osx
arch=i386 build_all_osx

echo "building universal/ffmpeg2theora"
tmp_dir=/tmp/ffmpeg2theora
build_dir=$tmp_dir/build
dist_dir=$tmp_dir/dist
mkdir -p $dist_dir/universal/usr/local/bin
mkdir -p $dist_dir/universal/usr/local/share/man/man1/
cp $dist_dir/i386/share/man/man1/ffmpeg2theora.1 $dist_dir/universal/usr/local/share/man/man1/
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

