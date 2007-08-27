from distutils.core import setup
from py2app.build_app import py2app

import os
import shutil

class mypy2app(py2app):
  def run(self):
    py2app.run(self)
    print ">>>>> installing ffmpeg2theora <<<<<<"
    resourcesRoot = os.path.join(self.dist_dir, 'Simple Theora Encoder.app/Contents/Resources')
    shutil.copy('ffmpeg2theora', os.path.join(resourcesRoot, 'ffmpeg2theora'))
    rsrc_file = "Simple Theora Encoder.rsrc.py"
    shutil.copy(rsrc_file, os.path.join(resourcesRoot, rsrc_file))

    imgPath = os.path.join(self.dist_dir, "Simple Theora Encoder.dmg")
    os.system('''hdiutil create -srcfolder "%s" -volname "Simple Theora Encoder" -format UDZO "%s"''' %
                (self.dist_dir, os.path.join(self.dist_dir, "Simple Theora Encoder.tmp.dmg")))
    os.system('''hdiutil convert -format UDZO -imagekey zlib-level=9 -o "%s" "%s"''' %
                (imgPath, os.path.join(self.dist_dir, "Simple Theora Encoder.tmp.dmg")))
    os.remove(os.path.join(self.dist_dir,"Simple Theora Encoder.tmp.dmg"))

setup(
  app=['Simple Theora Encoder.py'],
  name='Simple Theora Encoder',
  options={'py2app': {
	'strip': True,
	'optimize': 2,
	'iconfile': 'Simple Theora Encoder.icns',
	'packages': ['PythonCard', 'wx'],
	'plist': {'CFBundleIconFile': 'Simple Theora Encoder.icns'},
  }},
  cmdclass = {'py2app': mypy2app }
  
)


