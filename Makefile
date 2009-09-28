all:
	scons

clean:
	scons -c

install:
	scons install $(PREFIX)

dist:
	svn export . ffmpeg2theora-`grep ^pkg_version= SConstruct | cut -d\" -f2`
	tar cjf ffmpeg2theora-`grep ^pkg_version= SConstruct | cut -d\" -f2`.tar.bz2 ffmpeg2theora-`grep ^pkg_version= SConstruct | cut -d\" -f2`
	rm -r ffmpeg2theora-`grep ^pkg_version= SConstruct | cut -d\" -f2`
