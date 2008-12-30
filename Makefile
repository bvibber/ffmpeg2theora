all:
	scons

clean:
	scons -c

install:
	scons install $(PREFIX)

dist:
	bzr export ffmpeg2theora-`grep pkg_version= SConstruct | cut -d\" -f2`.tar.bz2
