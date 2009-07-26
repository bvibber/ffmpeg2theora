#!/usr/bin/env python
# vi:si:et:sw=2:sts=2:ts=2
# -*- coding: utf-8 -*-
# Written 2007 by j@v2v.cc
#
# see LICENSE.txt for license information
#
__version__ = "1.0"

import os
from os.path import join, dirname, basename, abspath
import sys
import time
import thread
import xmlrpclib

import wx
try:
  from xml.etree.ElementTree import Element, SubElement, ElementTree, parse
except:
  from elementtree.ElementTree import Element, SubElement, ElementTree, parse

from theoraenc.addVideoDialog import addVideoDialog
from theoraenc import theoraenc

#overwrite location of resources in submodules
if os.name != 'nt':
  theoraenc.resourcePath = abspath(dirname(__file__))

class SimpleTheoraEncoder(wx.Frame):
  queuedata = {}
  _qd_key = {}
  encodingQueueInitialized = False
  inputFile = False
  encoding = False
  quit = False


  def initMainInterface(self):
    #TODO: addd menue
    
    self.encodingQueue = wx.ListCtrl(self, -1, style=wx.LC_REPORT)
    self.encodingQueue.SetPosition(wx.Point(10,50))
    self.encodingQueue.SetSize(wx.Size(440, 165))
    self.encodingQueue.Bind(wx.EVT_LIST_ITEM_SELECTED, self.OnItemSelected)
    
    buttonSize = wx.Size(80,-1)
    self.addItem = wx.Button(self, wx.ID_ANY, "Add...", wx.Point(460, 70), buttonSize)
    self.Bind(wx.EVT_BUTTON, self.OnClickAdd, self.addItem)
    
    self.removeItem = wx.Button(self, wx.ID_ANY, "Remove", wx.Point(460, 100), buttonSize)
    self.Bind(wx.EVT_BUTTON, self.OnClickRemove, self.removeItem)
    self.removeItem.Disable()
    
    self.buttonEncode = wx.Button(self, wx.ID_ANY, "Encode", wx.Point(460, 190), buttonSize)
    self.Bind(wx.EVT_BUTTON, self.OnEncode, self.buttonEncode)
    self.buttonEncode.Disable()

    self.Bind(wx.EVT_CLOSE, self.OnClose)

    #Title
    titleFont = wx.Font(14, wx.DEFAULT, wx.NORMAL, wx.NORMAL, False, u'Sans')
    self.title = wx.StaticText(self, -1, "Simple Theora Encoder", wx.Point(10, 10))
    self.title.SetFont(titleFont)
    
  def __init__(self, parent, id, title, inputFile=None):
    wx.Frame.__init__(self, parent, id, title, size=(550,230))
    self.inputFile = inputFile
    self.initMainInterface()
    self.Show(True)
    
    if self.addItem.IsEnabled:
      self.OnClickAdd(None)

  def initializeUploadQueue(self, selectItem = 0):
      q = self.encodingQueue
      q.ClearAll()
      q.InsertColumn(0, "Name")
      q.InsertColumn(1, "Stats")
      q.SetColumnWidth(0, 200)
      q.SetColumnWidth(1, 240)
      
      q.itemDataMap = self.queuedata
      
      items = self.queuedata.items()
      for x in range(len(items)):
        key, item = items[x]
        self.queuedata[key]['itemID'] = x
        self._qd_key[item['name']] = key
        q.InsertStringItem(x, item['path'])
        q.SetStringItem(x, 0, item['display_path'])
        q.SetStringItem(x, 1, item['status'])
        q.SetItemData(x, key)
      
      
      # show how to select an item
      self.currentItem = selectItem
      if items:
        q.SetItemState(self.currentItem, wx.LIST_STATE_SELECTED, wx.LIST_STATE_SELECTED)
      else:
        self.removeItem.Disable()
      self.encodingQueueInitialized = True
  
  def setItemStatus(self, itemID, value):
    key = self.encodingQueue.GetItemData(itemID)
    self.queuedata[key]['status'] = value
    self.encodingQueue.SetStringItem(itemID, 1, value)
  
  def updateItemStatus(self, name, status):
    try:
      item = self.queuedata[self._qd_key[name]]
    except KeyError:
      return
    itemID = item['itemID']
    if item['status'] != status:
      item['status'] = status
      #self.title.SetLabel(os.path.basename(item['path']) +': '+ status)
      self.encodingQueue.SetStringItem(itemID, 1, status)

  def getSettings(self, options):
    settings = []
    for key in ('width', 'height'):
      if key in options and options[key]:
        settings.append('--%s' % key)
        settings.append("%s" % int(options[key]))
    for key in ('videoquality', 'audioquality'):
      if key in options and options[key]:
        settings.append('--%s' % key)
        settings.append("%s" % float(options[key]))
    if 'subtitles' in options and options['subtitles']:
      for s in options['subtitles']:
        settings.append('--subtitles')
        settings.append('%s' % s['file'])
        settings.append('--subtitles-language')
        settings.append('%s' % s['language'])
        settings.append('--subtitles-category')
        settings.append('%s' % s['category'])
        settings.append('--subtitles-encoding')
        settings.append('%s' % s['encoding'])
    return settings

  def encodeItem(self, item):
    item['encoding'] = True
    if self.currentItem == item['itemID']:
      self.removeItem.SetLabel('Cancel')
    self.setItemStatus(item['itemID'], 'encoding')
    result = item['enc'].encode()
    if not result:
      self.setItemStatus(item['itemID'], 'encoding failed.')
    else:
      self.setItemStatus(item['itemID'], 'encoding done.')

    item['encoded'] = True
    item['encoding'] = False
    return result

  def encodeQueue(self, foo):
    def nextItem():
      items = self.queuedata.items()
      for x in range(len(items)):
        key, item = items[x]
        if not item['encoded']:
          return item
      return None

    next = nextItem()
    while next and not self.quit:
      self.encodeItem(next)
      next = nextItem()
    self.encoding = False

  def addItemToQueue(self, videoFile, options):
    name = os.path.basename(videoFile)
    display_path = videoFile
    if len(display_path) > 25:
      display_path = "..." + display_path[-24:]
    item = dict(
      path = videoFile, 
      options = options,
      display_path = display_path, 
      status = 'waiting...           ',
      listID = 0,
      name = name,
    )
    item['encoding'] = False
    item['encoded'] = False
    item['enc'] = theoraenc.TheoraEnc(videoFile, None, lambda x: self.updateItemStatus(name, x))
    item['enc'].settings = self.getSettings(options)

    if self.encodingQueueInitialized:
      x = self.encodingQueue.GetItemCount()
      if self.queuedata:
        key = max(self.queuedata.keys()) + 1
      else:
        key = 1
      item['itemID'] = x
      self.queuedata[key] = item
      self.encodingQueue.InsertStringItem(x, item['path'])
      self.encodingQueue.SetStringItem(x, 0, item['display_path'])
      self.encodingQueue.SetStringItem(x, 1, item['status'])
      self.encodingQueue.SetItemData(x, key)
    else:
      key = 1
      self.queuedata[key] = item
      self.initializeUploadQueue()
    self._qd_key[name] = key

  def OnItemSelected(self, event):
    self.currentItem = event.m_itemIndex
    key = self.encodingQueue.GetItemData(self.currentItem)
    item = self.queuedata[key]
    if item['encoding']:
        self.removeItem.SetLabel('Cancel')
    else:
        self.removeItem.SetLabel('Remove')
    self.removeItem.Enable()
  
  def OnClickAdd(self, event):
    result = addVideoDialog(self, theoraenc.hasKate, theoraenc.hasIconv)
    time.sleep(0.5)
    if result['ok']:
      self.addItemToQueue(result['videoFile'], result)
      if not self.encoding:
        self.buttonEncode.Enable()
  
  def OnClickRemove(self, event):
    key = self.encodingQueue.GetItemData(self.currentItem)
    if 'enc' in self.queuedata[key]:
      self.queuedata[key]['enc'].cancel()
    del self.queuedata[key]
    self.initializeUploadQueue(self.currentItem)

  def OnEncode(self, event):
    if not self.encoding:
      self.encoding = True
      thread.start_new_thread(self.encodeQueue, ("foo", ))
      self.buttonEncode.Disable()

  def OnClose(self, event):
    close = True
    if self.encoding:
      dlg = wx.MessageDialog(self, 
          "Videos are still encoded.\nDo you really want to close Simple Theora Encoder?",
          "Confirm Exit", wx.OK|wx.CANCEL|wx.ICON_QUESTION)
      result = dlg.ShowModal()
      dlg.Destroy()
      if result != wx.ID_OK:
        close = False
    if close:
      self.quit = True
      for key in self.queuedata:
        if 'enc' in self.queuedata[key]:
          try:
            self.queuedata[key]['enc'].cancel()
          except:
            pass
      self.Destroy()

def gui(inputFile = None):
  app = wx.PySimpleApp()
  frame=SimpleTheoraEncoder(None, wx.ID_ANY, 'Simple Theora Encoder', inputFile = inputFile)
  app.MainLoop()
  
if __name__ == '__main__':
  inputFile = None
  if len(sys.argv) > 1 and not sys.argv[1].startswith('-'):
    inputFile = sys.argv[1]
  gui(inputFile)
