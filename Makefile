all:
	scons

clean:
	scons -c

install:
	scons install $(PREFIX)

dist:
	git archive --format=tar --prefix=ffmpeg2theora-`./version.sh`/ master | bzip2 >ffmpeg2theora-`./version.sh`.tar.bz2
	ls -lah ffmpeg2theora-`./version.sh`.tar.bz2
