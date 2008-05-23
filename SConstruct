# SCons build specification
from glob import glob
import os

import SCons


pkg_version="0.21+svn"
pkg_name="ffmpeg2theora"

#parse config variables
static = False

env = Environment()
pkg_flags="--cflags --libs"
if static:
  pkg_flags+=" --static"
  env.Append(LINKFLAGS=["-static"])

env.Append(CPPPATH=['.'])
env.Append(CCFLAGS=[
  '-DPACKAGE_VERSION=\\"%s\\"' %pkg_version,
  '-DPACKAGE_STRING=\\"%s-%s\\"' %(pkg_name, pkg_version),
  '-DPACKAGE=\\"%s\\"' % pkg_name,
  '-DVERSION=\\"%s\\"' %pkg_version,
])
#if env['CC'] == 'gcc':
#  env.Append(CCFLAGS=["-g", "-O2", "-Wall"])

def CheckPKGConfig(context, version): 
  context.Message( 'Checking for pkg-config... ' ) 
  ret = context.TryAction('pkg-config --atleast-pkgconfig-version=%s' % version)[0] 
  context.Result( ret ) 
  return ret 

def CheckPKG(context, name): 
  context.Message( 'Checking for %s... ' % name )
  if os.environ.get('PKG_CONFIG_PATH', ''):
    action = 'PKG_CONFIG_PATH=%s pkg-config --exists "%s"' % (os.environ['PKG_CONFIG_PATH'], name)
  else:
    action = 'pkg-config --exists "%s"' % name
  ret = context.TryAction(action)[0]
  context.Result( ret ) 
  return ret

conf = Configure(env, custom_tests = {
  'CheckPKGConfig' : CheckPKGConfig,
  'CheckPKG' : CheckPKG,
  })
  
if not conf.CheckPKGConfig('0.15.0'): 
   print 'pkg-config >= 0.15.0 not found.' 
   Exit(1)

XIPH_LIBS="ogg >= 1.1 vorbis vorbisenc theora >= 1.0beta1"

if not conf.CheckPKG(XIPH_LIBS): 
  print 'some xiph libs are missing, ffmpeg2theora depends on %s' % XIPH_LIBS
  Exit(1) 
env.ParseConfig('pkg-config %s "%s"' % (pkg_flags, XIPH_LIBS))

FFMPEG_LIBS="libavformat libavcodec libavdevice libswscale libpostproc"
if os.path.exists("./ffmpeg"):
  os.environ['PKG_CONFIG_PATH'] = "./ffmpeg:" + os.environ.get('PKG_CONFIG_PATH', '')
if not conf.CheckPKG(FFMPEG_LIBS): 
  print """
      Could not find %s.
      You can install it via
       sudo apt-get install %s
      or update PKG_CONFIG_PATH to point to ffmpeg's source folder
      or run ./get_ffmpeg_svn.sh (for more information see INSTALL)
  """ %(FFMPEG_LIBS, " ".join(["%s-dev"%l for l in FFMPEG_LIBS.split()]))
  Exit(1) 
env.ParseConfig('pkg-config %s "%s"' % (pkg_flags, FFMPEG_LIBS))

KATE_LIBS="oggkate"
if os.path.exists("./libkate/pkg/pkgconfig"):
  os.environ['PKG_CONFIG_PATH'] = "./libkate/pkg/pkgconfig:" + os.environ.get('PKG_CONFIG_PATH', '')
if conf.CheckPKG(KATE_LIBS):
  env.ParseConfig('pkg-config %s "%s"' % (pkg_flags, KATE_LIBS))
  env.Append(CCFLAGS=['-DHAVE_KATE', '-DHAVE_OGGKATE'])
else:
  print """
      Could not find %s.
      update PKG_CONFIG_PATH to point to ffmpeg's source folder
      or run ./get_libkate.sh (for more information see INSTALL)
  """ % KATE_LIBS
env = conf.Finish()

# ffmpeg2theora 
ffmpeg2theora = env.Copy()
ffmpeg2theora_sources = glob('*.c')
ffmpeg2theora.Program('ffmpeg2theora', ffmpeg2theora_sources)
