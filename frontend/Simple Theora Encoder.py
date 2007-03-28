#!/usr/bin/envpython

"""
__version__ = "1.0"
"""

from PythonCard import model, dialog
import wx

import os
from os.path import join, dirname, basename

import thread


"""
  Format seconds
"""
def sec2time(seconds):
  seconds = int(seconds)
  minutes = int(seconds / 60)
  seconds = seconds % 60
  hours = int(minutes / 60)
  minutes = minutes % 60
  return "%02d:%02d:%02d" % (hours, minutes, seconds)


class Encoder:
  working = False
  inputfile = ''
  outputfile = ''
  settings = "-p preview"
  ffmpeg2theora_path = os.path.abspath(join(dirname(__file__), 'ffmpeg2theora'))
  
  def commandline(self):
    cmd = "'%s' --frontend %s '%s'" % (self.ffmpeg2theora_path, self.settings, self.inputfile.replace("'", "\'"))
    if self.outputfile:
      cmd += " -o '%s'" % self.outputfile.replace("'", "\'")
    cmd += " 2>&1"
    return cmd
    
    
  def encodeItem(self, item):
    self.inputfile = self.queuedata[item]['path']
    self.settings = self.queuedata[item]['settings']
    itemID = self.queuedata[item]['itemID']
    self.itemStatus(itemID, 'encoding...')
    cmd = self.commandline()
    f = os.popen(cmd)
    line = f.readline()
    info = dict()
    while line:
      if line.startswith('f2t'):
        for o in line.split(';')[1:]:
          oo = o.split(': ')
          if len(oo) >= 2:
            info[oo[0]] = ": ".join(oo[1:]).strip()
        if info.has_key('position'):
          encoded = "encoded %s/" % sec2time(float(info['position']))
          if info.has_key('duration') and float(info['duration']):
            encoded =  "% 3d %% done,  " % ((float(info['position']) / float(info['duration'])) * 100)
          line = encoded + 'remaining: '+ sec2time(float(info['remaining']))
        else:
          line = "encoding.."
        self.itemStatus(itemID, line)
      line = f.readline()
    f.close()
    if info.get('result', 'no') == 'ok':
      self.itemStatus(itemID, 'Done.')
    else:
      self.itemStatus(itemID, info.get('result', 'Failed.'))
      
  def encodeQueueThread(self):
    self.working = True 
    for key in self.queuedata:
      if self.queuedata[key]['status'] != 'Done.':
        self.encodeItem(key)
    self.working = False
    self.encodingFinished()
    
  def encodeQueue(self, queuedata, itemStatus, encodingFinished):
    self.itemStatus = itemStatus
    self.encodingFinished = encodingFinished
    self.queuedata = queuedata
    encoding_thread = thread.start_new_thread(self.encodeQueueThread, ())
  


class SimpleTheoraEncoderBackground(model.Background):
    encoder = Encoder()
    queuedata = {}
    encodingQueueInitialized = False
    
    def on_initialize(self,event):
        list = self.components.encodingQueue
        list.InsertColumn(0, "Name")
        list.InsertColumn(1, "Status")
        pass

    # so how much to wrap and how much to leave raw wxPython?
    def initializeEncodingQueue(self, select = 0):
        list = self.components.encodingQueue
        list.Clear()
        items = self.queuedata.items()

        for x in range(len(items)):
            key, item = items[x]
            self.queuedata[key]['itemID'] = x
            list.InsertStringItem(x, item['path'])
            list.SetStringItem(x, 0, item['display_path'])
            list.SetStringItem(x, 1, item['status'])
            list.SetItemData(x, key)

        list.SetColumnWidth(0, 180)
        list.SetColumnWidth(1, 250)

        list.SetItemDataMap(self.queuedata)

        # show how to select an item
        self.currentItem = select
        if items:
          list.SetItemState(self.currentItem, wx.LIST_STATE_SELECTED, wx.LIST_STATE_SELECTED)

        self.encodingQueueInitialized = True
        self.components.encodeQueue.enabled = True
        
    def setItemStatus(self, itemID, value):
      key = self.components.encodingQueue.GetItemData(self.currentItem)
      self.queuedata[key]['status'] = value
      self.components.encodingQueue.SetStringItem(itemID, 1, value)
      
    def addItemToQueue(self, input_path, settings):
      list = self.components.encodingQueue
      display_path = input_path
      if len(display_path) > 26:
        display_path = "..." + input_path[-23:]
      
      item = dict(
        path = input_path, 
        settings = settings,
        display_path = display_path, 
        status = 'waiting...                 ',
        listID = 0
      )
      if self.encodingQueueInitialized:
        x = list.GetItemCount()
        if self.queuedata:
          key = max(self.queuedata.keys()) + 1
        else:
          key = 1
        item['itemID'] = x
        self.queuedata[key] = item
        list.InsertStringItem(x, item['path'])
        list.SetStringItem(x, 0, item['display_path'])
        list.SetStringItem(x, 1, item['status'])
        list.SetItemData(x, key)
      else:
        key = 1        
        self.queuedata[key] = item
        self.initializeEncodingQueue()
              
    def on_addItem_mouseClick(self, event):
      wildcard = "Video files|*.AVI;*.avi;*.OGG;*.ogg;*.mov;*.MOV;*.dv;*.DV;*.mp4;*.MP4;*.mpg;*.mpeg;*.wmv;*.MPG;*.flv;*.FLV|All Files (*.*)|*.*"
      
      result = dialog.fileDialog(self, 'Add Video..', '', '', wildcard )
      if result.accepted:
        for input_path in result.paths:
          settings = '-p preview'
          self.addItemToQueue(input_path, settings)
    
    def on_removeItem_mouseClick(self, event):
      list = self.components.encodingQueue
      print "remove", self.currentItem
      key = self.components.encodingQueue.GetItemData(self.currentItem)
      self.queuedata.pop(key)
      self.initializeEncodingQueue(self.currentItem)

    def on_editItem_mouseClick(self, event):
      print "edit settings here"
      list = self.components.encodingQueue
      key = self.components.encodingQueue.GetItemData(self.currentItem)
      result = dialog.textEntryDialog(self, 
                                  'This parametes are passed to ffmpeg2theora adjust to your needs',
                                  'Encoding Settings', 
                                  self.queuedata[key]['settings'])
      if result.accepted:
        self.queuedata[key]['settings'] = result.text
      
    def on_encodeQueue_mouseClick(self, event):
      self.components.addItem.enabled = False
      self.components.editItem.enabled = False
      self.components.removeItem.enabled = False
      self.components.encodeQueue.enabled = False
      self.encoder.encodeQueue(self.queuedata, self.setItemStatus, self.encodingFinished)
      
    def encodingFinished(self):
      self.components.addItem.enabled = True
      self.components.editItem.enabled = True
      self.components.removeItem.enabled = True
      self.components.encodeQueue.enabled = True
      
      
    def on_encodingQueue_select(self, event):
        self.currentItem = event.m_itemIndex
        key = self.components.encodingQueue.GetItemData(self.currentItem)
        print self.queuedata[key]
        self.components.editItem.enabled = True
        self.components.removeItem.enabled = True


if __name__ == '__main__':
    app = model.Application(SimpleTheoraEncoderBackground)
    app.MainLoop()
