# -*- coding: utf-8 -*-
# vi:si:et:sw=2:sts=2:ts=2

import os
from os.path import exists, join, dirname, abspath
import time
import sys
import signal
import subprocess
import threading

import simplejson
import wx


resourcePath = abspath(dirname(__file__))

def probe_ffmpeg2theora():
  appname = 'ffmpeg2theora'
  if os.name == 'nt':
    appname = appname + '.exe'
  ffmpeg2theora = join(resourcePath, appname)
  if not exists(ffmpeg2theora):
    # ffmpeg2theora is likely in $resourcePath/../.. since we're in frontend
    ffmpeg2theora = join(resourcePath, join('../../', appname))
    if not exists(ffmpeg2theora):
      ffmpeg2theora = join('./', appname)
      if not exists(ffmpeg2theora):
        ffmpeg2theora = appname
  return ffmpeg2theora

def probe_kate(ffmpeg2theora):
  hasKate = False
  p = subprocess.Popen([ffmpeg2theora, '--help'], shell=False, stdout=subprocess.PIPE, close_fds=True)
  data, err = p.communicate()
  if 'Subtitles options:' in data:
    hasKate = True
  return hasKate

def probe_iconv(ffmpeg2theora):
  hasIconv = False
  p = subprocess.Popen([ffmpeg2theora, '--help'], shell=False, stdout=subprocess.PIPE, close_fds=True)
  data, err = p.communicate()
  if 'supported are all encodings supported by iconv' in data:
      hasIconv = True
  return hasIconv

def timestr(seconds):
  hours   = int(seconds/3600)
  minutes = int((seconds-( hours*3600 ))/60)
  seconds = (seconds-((hours*3600)+(minutes*60)))
  return '%02d:%02d:%02d' % (hours, minutes, seconds)

class ThreadWorker(threading.Thread):
    def __init__(self, callable, *args, **kwargs):
        super(ThreadWorker, self).__init__()
        self.callable = callable
        self.args = args
        self.kwargs = kwargs
        self.setDaemon(True)

    def run(self):
        try:
            self.callable(*self.args, **self.kwargs)
        except wx.PyDeadObjectError:
            pass
        except Exception, e:
            print e

class TheoraEnc:
  settings = []
  p = None

  def __init__(self, inputFile, outputFile, updateGUI):
    self.inputFile = inputFile
    self.outputFile = outputFile
    self.updateGUI = updateGUI
  
  def commandline(self):
    cmd = []
    cmd.append(ffmpeg2theora)
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
    p = subprocess.Popen(cmd, shell=False, stdout=subprocess.PIPE, close_fds=True)
    self.p = p
    info = dict()
    status = ''
    self.warning_timeout = 0

    def worker(pipe):
      while True:
        line = pipe.readline()
        if line == '':
          break
        else:
          now = time.time()
          try:
            data = simplejson.loads(line)
            for key in data:
              info[key] = data[key]
            if 'WARNING' in info:
              status = info['WARNING']
              self.warning_timeout = now + 3
              del info['WARNING']
            else:
              status=None
              if now >= self.warning_timeout:
                if 'position' in info:
                  if 'duration' in info and float(info['duration']):
                    encoded =  "encoding % 3d %% done " % ((float(info['position']) / float(info['duration'])) * 100)
                  else:
                    encoded = "encoded %s/" % timestr(float(info['position']))
                  if float(info['remaining'])>0:
                    status = encoded + '/ '+ timestr(float(info['remaining']))
                  else:
                    status = encoded
                  status =  "encoding % 3d %% done " % ((float(info['position']) / float(info['duration'])) * 100)
                else:
                  status = "encoding.."
            if status != None:
              self.updateGUI(status)
          except:
            pass

    stdout_worker = ThreadWorker(worker, p.stdout)
    stdout_worker.start()

    p.wait()

    if info.get('result', 'no') == 'ok':
      self.updateGUI('Encoding done.')
      return True
    else:
      self.updateGUI(info.get('result', 'Encoding failed.'))
      return False


def fileInfo(filename):
  cmd = []
  cmd.append(ffmpeg2theora)
  cmd.append('--info')
  cmd.append(filename)
  p = subprocess.Popen(cmd, shell=False, stdout=subprocess.PIPE, close_fds=True)
  data, err = p.communicate()
  try:
    info = simplejson.loads(data)
  except:
    info = None
  return info

ffmpeg2theora = probe_ffmpeg2theora()
hasKate = probe_kate(ffmpeg2theora)
hasIconv = probe_iconv(ffmpeg2theora)

