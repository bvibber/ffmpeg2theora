prefix=/Users/j/local
outdir=/Users/j/Desktop
export PATH=$prefix/bin:$PATH
export PKG_CONFIG_PATH=$prefix/lib/pkgconfig/

./configure --prefix=$prefix/local/ --with-ffmpegprefix=$prefix/src/ffmpeg-0.4.9-pre1 && make || exit
strip ffmpeg2theora
mkdir inst
mkdir inst/ffmpeg2theora
cp ffmpeg2theora inst/ffmpeg2theora

cd inst
hdiutil create -srcfolder 'ffmpeg2theora'  ffmpeg2theora.tmp
hdiutil convert ffmpeg2theora.tmp.dmg  -format UDZO -o ffmpeg2theora
rm -f $outdir/ffmpeg2theora.dmg
mv ffmpeg2theora.dmg $outdir
cd ..
rm -rf inst
