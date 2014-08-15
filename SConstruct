# SCons build specification
# vi:si:et:sw=2:sts=2:ts=2
from glob import glob
import os

import SCons

def version():
    #return "0.29"
    f = os.popen("./version.sh")
    version = f.read().strip()
    f.close()
    return version

pkg_version=version()

pkg_name="ffmpeg2theora"

scons_version=(1,2,0)

try:
    EnsureSConsVersion(*scons_version)
except TypeError:
    print 'SCons %d.%d.%d or greater is required, but you have an older version' % scons_version
    Exit(2)

opts = Variables()
opts.AddVariables(
  BoolVariable('static', 'Set to 1 for static linking', 0),
  BoolVariable('debug', 'Set to 1 to enable debugging', 0),
  BoolVariable('build_ffmpeg', 'Set to 1 to build local copy of ffmpeg', 0),
  ('prefix', 'install files in', '/usr/local'),
  ('bindir', 'user executables', 'PREFIX/bin'),
  ('mandir', 'man documentation', 'PREFIX/man'),
  ('destdir', 'extra install time prefix', ''),
  ('APPEND_CCFLAGS', 'Additional C/C++ compiler flags'),
  ('APPEND_LINKFLAGS', 'Additional linker flags'),
  BoolVariable('libkate', 'enable libkate support', 1),
  BoolVariable('crossmingw', 'Set to 1 for crosscompile with mingw', 0)
)
env = Environment(options = opts)
Help(opts.GenerateHelpText(env))

pkg_flags="--cflags --libs"
if env['static']:
  pkg_flags+=" --static"
  env.Append(LINKFLAGS=["-static"])

if env['crossmingw']:
    env.Tool('crossmingw', toolpath = ['scons-tools'])

prefix = env['prefix']
if env['destdir']:
  if prefix.startswith('/'): prefix = prefix[1:]
  prefix = os.path.join(env['destdir'], prefix)
man_dir = env['mandir'].replace('PREFIX', prefix)
bin_dir = env['bindir'].replace('PREFIX', prefix)

env.Append(CPPPATH=['.'])
env.Append(CCFLAGS=[
  '-DPACKAGE_VERSION=\\"%s\\"' % pkg_version,
  '-DPACKAGE_STRING=\\"%s-%s\\"' % (pkg_name, pkg_version),
  '-DPACKAGE=\\"%s\\"' % pkg_name,
  '-D_FILE_OFFSET_BITS=64'
])

env.Append(CCFLAGS = Split('$APPEND_CCFLAGS'))
env.Append(LINKFLAGS = Split('$APPEND_LINKFLAGS'))

if env['debug'] and env['CC'] == 'gcc':
  env.Append(CCFLAGS=["-g", "-O2", "-Wall"])

if GetOption("help"):
    Return()

def ParsePKGConfig(env, name): 
  if os.environ.get('PKG_CONFIG_PATH', ''):
    action = 'PKG_CONFIG_PATH=%s pkg-config %s "%s"' % (os.environ['PKG_CONFIG_PATH'], pkg_flags, name)
  else:
    action = 'pkg-config %s "%s"' % (pkg_flags, name)
  return env.ParseConfig(action)

def TryAction(action):
    import os
    ret = os.system(action)
    if ret == 0:
        return (1, '')
    return (0, '')

def CheckPKGConfig(context, version): 
  context.Message( 'Checking for pkg-config... ' ) 
  ret = TryAction('pkg-config --atleast-pkgconfig-version=%s' % version)[0] 
  context.Result( ret ) 
  return ret 

def CheckPKG(context, name): 
  context.Message( 'Checking for %s... ' % name )
  if os.environ.get('PKG_CONFIG_PATH', ''):
    action = 'PKG_CONFIG_PATH=%s pkg-config --exists "%s"' % (os.environ['PKG_CONFIG_PATH'], name)
  else:
    action = 'pkg-config --exists "%s"' % name
  ret = TryAction(action)[0]
  context.Result( ret ) 
  return ret

env.PrependENVPath ('PATH', os.environ['PATH'])

conf = Configure(env, custom_tests = {
  'CheckPKGConfig' : CheckPKGConfig,
  'CheckPKG' : CheckPKG,
})

if env["build_ffmpeg"]:
  if env.GetOption('clean'):
    TryAction("cd ffmpeg;make distclean")
  else:
    TryAction("./build_ffmpeg.sh")

if not env.GetOption('clean'):
  pkgconfig_version='0.15.0'
  if not conf.CheckPKGConfig(pkgconfig_version): 
     print 'pkg-config >= %s not found.' % pkgconfig_version 
     Exit(1)

  if not conf.CheckPKG("ogg >= 1.1"): 
    print 'ogg >= 1.1 missing'
    Exit(1) 

  if not conf.CheckPKG("vorbis"): 
    print 'vorbis missing'
    Exit(1) 

  if not conf.CheckPKG("vorbisenc"): 
    print 'vorbisenc missing'
    Exit(1) 

  if not conf.CheckPKG("theoraenc >= 1.1.0"): 
    print 'theoraenc >= 1.1.0 missing'
    Exit(1) 

  XIPH_LIBS="ogg >= 1.1 vorbis vorbisenc theoraenc >= 1.1.0"

  if not conf.CheckPKG(XIPH_LIBS): 
    print 'some xiph libs are missing, ffmpeg2theora depends on %s' % XIPH_LIBS
    Exit(1) 
  ParsePKGConfig(env, XIPH_LIBS)

  FFMPEG_LIBS=[
      "libavdevice",
      "libavformat",
      "libavfilter",
      "libavcodec >= 52.30.0",
      "libpostproc",
      "libswscale",
      "libswresample",
      "libavutil",
  ]
  if os.path.exists("./ffmpeg"):
    pkg_path = list(set(map(os.path.dirname, glob('./ffmpeg/*/*.pc'))))
    pkg_path.append(os.environ.get('PKG_CONFIG_PATH', ''))
    os.environ['PKG_CONFIG_PATH'] = ':'.join(pkg_path)
    env.Append(CCFLAGS=[
      '-Iffmpeg'
    ])
    for lib in FFMPEG_LIBS:
      lib = lib.split(' ')[0]
      env.Append(LINKFLAGS=[
        '-Lffmpeg/' + lib
      ])

  if conf.CheckPKG('libswresample'):
    FFMPEG_LIBS.append('libswresample')
    env.Append(CCFLAGS=[
      '-DUSE_SWRESAMPLE'
    ])
  elif conf.CheckPKG('libavresample'):
    FFMPEG_LIBS.append('libavresample')

  if not conf.CheckPKG(' '.join(FFMPEG_LIBS)): 
    print """
        Could not find %s.
        You can install it via
         sudo apt-get install %s
        or update PKG_CONFIG_PATH to point to ffmpeg's source folder
        or run ./get_ffmpeg.sh (for more information see INSTALL)
    """ %(" ".join(FFMPEG_LIBS), " ".join(["%s-dev"%l.split()[0] for l in FFMPEG_LIBS]))
    Exit(1) 

  for lib in FFMPEG_LIBS:
      ParsePKGConfig(env, lib)

  if conf.CheckCHeader('libavformat/framehook.h'):
      env.Append(CCFLAGS=[
        '-DHAVE_FRAMEHOOK'
      ])

  KATE_LIBS="oggkate"
  if env['libkate']:
    if os.path.exists("./libkate/misc/pkgconfig"):
      os.environ['PKG_CONFIG_PATH'] = "./libkate/misc/pkgconfig:" + os.environ.get('PKG_CONFIG_PATH', '')
    if os.path.exists("./libkate/pkg/pkgconfig"):
      os.environ['PKG_CONFIG_PATH'] = "./libkate/pkg/pkgconfig:" + os.environ.get('PKG_CONFIG_PATH', '')
    if conf.CheckPKG(KATE_LIBS):
      ParsePKGConfig(env, KATE_LIBS)
      env.Append(CCFLAGS=['-DHAVE_KATE', '-DHAVE_OGGKATE'])
    else:
      print """
          Could not find libkate. Subtitles support will be disabled.
          You can also run ./get_libkate.sh (for more information see INSTALL)
          or update PKG_CONFIG_PATH to point to libkate's source folder
      """

  if conf.CheckCHeader('iconv.h'):
      env.Append(CCFLAGS=[
        '-DHAVE_ICONV'
      ])
      if conf.CheckLib('iconv'):
          env.Append(LIBS=['iconv'])

  if env['crossmingw']:
      env.Append(CCFLAGS=['-Wl,-subsystem,windows'])
      env.Append(LIBS=['m'])
  elif env['static']:
      env.Append(LIBS=['m', 'dl'])


env = conf.Finish()

# ffmpeg2theora 
ffmpeg2theora = env.Clone()
ffmpeg2theora_sources = glob('src/*.c')
ffmpeg2theora.Program('ffmpeg2theora', ffmpeg2theora_sources)

ffmpeg2theora.Install(bin_dir, 'ffmpeg2theora')
ffmpeg2theora.Install(man_dir + "/man1", 'ffmpeg2theora.1')
ffmpeg2theora.Alias('install', prefix)
