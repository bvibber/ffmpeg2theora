# -*- coding: utf-8 -*-
# vi:si:et:sw=2:sts=2:ts=2

import os
from os.path import exists, join, dirname, abspath
import time
import sys
import signal
import subprocess

resourcePath = abspath(dirname(__file__))

def timestr(seconds):
  hours   = int(seconds/3600)
  minutes = int((seconds-( hours*3600 ))/60)
  seconds = (seconds-((hours*3600)+(minutes*60)))
  return '%02d:%02d:%02d' % (hours, minutes, seconds)

class TheoraEnc:
  settings = []
  p = None
  def __init__(self, inputFile, outputFile, updateGUI):
    self.inputFile = inputFile
    self.outputFile = outputFile
    self.updateGUI = updateGUI
    appname = 'ffmpeg2theora'
    if os.name == 'nt':
      appname = appname + '.exe'
    self.ffmpeg2theora = join(resourcePath, appname)
    if not exists(self.ffmpeg2theora):
      self.ffmpeg2theora = appname
  
  def commandline(self):
    cmd = []
    cmd.append(self.ffmpeg2theora)
    cmd.append('--frontend')
    for e in self.settings:
      cmd.append(e)
    cmd.append(self.inputFile)
    if self.outputFile:
      cmd.append('-o')
      cmd.append(self.outputFile)
    return cmd
  
  def cancel(self):
    if self.p:
      print self.p.pid
      p = self.p.pid
      os.kill(p, signal.SIGTERM)
      t = 2.5  # max wait time in secs
      while self.p.poll() < 0:
        if t > 0.5:
          t -= 0.25
          time.sleep(0.25)
        else:  # still there, force kill
          try:
            os.kill(p, signal.SIGKILL)
            time.sleep(0.5)
            p.poll() # final try
          except:
            pass
          break
      #self.p.terminate()
 
  def encode(self):
    cmd = self.commandline()
    print cmd
    p = subprocess.Popen(cmd, shell=False, stderr=subprocess.PIPE, close_fds=True)
    self.p = p
    f = p.stderr
    line = f.readline()
    info = dict()
    status = ''
    while line:
      if line.startswith('f2t'):
        for o in line.split(';')[1:]:
          oo = o.split(': ')
          if len(oo) >= 2:
            info[oo[0]] = ": ".join(oo[1:]).strip()
        if 'position' in info:
          if 'duration' in info and float(info['duration']):
            encoded =  "encoding % 3d %% done " % ((float(info['position']) / float(info['duration'])) * 100)
          else:
            encoded = "encoded %s/" % timestr(float(info['position']))
          if float(info['remaining'])>0:
            status = encoded + '/ '+ timestr(float(info['remaining']))
          else:
            status = encoded
        else:
          status = "encoding.."
        self.updateGUI(status)
      line = f.readline()
    f.close()
    if info.get('result', 'no') == 'ok':
      self.updateGUI('Encoding done.')
      return True
    else:
      self.updateGUI(info.get('result', 'Encoding failed.'))
      return False

