# SCons build specification
from glob import glob
import os

import SCons

def svnversion():
    f = os.popen("svnversion")
    version = f.read().strip()
    f.close()
    return version

pkg_version="0.25+svn" + svnversion()
pkg_name="ffmpeg2theora"

scons_version=(0,97,0)
#this is needed to make scons -h work, so not checking for it right now
#(i.e. ubuntu hardy only ships with 0.97..)
#scons_version=(0,98,0)

try:
    EnsureSConsVersion(*scons_version)
except TypeError:
    print 'SCons %d.%d.%d or greater is required, but you have an older version' % scons_version
    Exit(2)

opts = Options()
opts.AddOptions(
  BoolOption('static', 'Set to 1 for static linking', 0),
  BoolOption('debug', 'Set to 1 to enable debugging', 0),
  ('prefix', 'install files in', '/usr/local'),
  ('bindir', 'user executables', 'PREFIX/bin'),
  ('mandir', 'man documentation', 'PREFIX/man'),
  ('destdir', 'extra install time prefix', ''),
  ('APPEND_CCFLAGS', 'Additional C/C++ compiler flags'),
  ('APPEND_LINKFLAGS', 'Additional linker flags'),
  BoolOption('libkate', 'enable libkate support', 1),
  BoolOption('crossmingw', 'Set to 1 for crosscompile with mingw', 0)
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

conf = Configure(env, custom_tests = {
  'CheckPKGConfig' : CheckPKGConfig,
  'CheckPKG' : CheckPKG,
  })

pkgconfig_version='0.15.0'
if not conf.CheckPKGConfig(pkgconfig_version): 
   print 'pkg-config >= %s not found.' % pkgconfig_version 
   Exit(1)

XIPH_LIBS="ogg >= 1.1 vorbis vorbisenc theoraenc >= 1.1.0"

if not conf.CheckPKG(XIPH_LIBS): 
  print 'some xiph libs are missing, ffmpeg2theora depends on %s' % XIPH_LIBS
  Exit(1) 
ParsePKGConfig(env, XIPH_LIBS)

FFMPEG_LIBS="libavcodec libavformat libavdevice libpostproc libswscale"
if os.path.exists("./ffmpeg"):
  os.environ['PKG_CONFIG_PATH'] = "./ffmpeg/libavutil:./ffmpeg/libavformat:./ffmpeg/libavcodec:./ffmpeg/libavdevice:./ffmpeg/libswscale:./ffmpeg/libpostproc:" + os.environ.get('PKG_CONFIG_PATH', '')
if not conf.CheckPKG(FFMPEG_LIBS): 
  print """
      Could not find %s.
      You can install it via
       sudo apt-get install %s
      or update PKG_CONFIG_PATH to point to ffmpeg's source folder
      or run ./get_ffmpeg_svn.sh (for more information see INSTALL)
  """ %(FFMPEG_LIBS, " ".join(["%s-dev"%l for l in FFMPEG_LIBS.split()]))
  Exit(1) 
for lib in FFMPEG_LIBS.split():
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


env = conf.Finish()

# ffmpeg2theora 
ffmpeg2theora = env.Clone()
ffmpeg2theora_sources = glob('src/*.c')
ffmpeg2theora.Program('ffmpeg2theora', ffmpeg2theora_sources)

ffmpeg2theora.Install(bin_dir, 'ffmpeg2theora')
ffmpeg2theora.Install(man_dir + "/man1", 'ffmpeg2theora.1')
ffmpeg2theora.Alias('install', prefix)

