# -*- coding: utf-8 -*-
# vi:si:et:sw=2:sts=2:ts=2

import os
from os.path import basename
import time

import wx

class AddVideoDialog(wx.Dialog):
  def __init__(
          self, parent, ID, title, size=wx.DefaultSize, pos=wx.DefaultPosition, 
          style=wx.DEFAULT_DIALOG_STYLE,
          ):
    
    self.videoFile = ''
    
    pre = wx.PreDialog()
    #pre.SetExtraStyle(wx.DIALOG_EX_CONTEXTHELP)
    pre.Create(parent, ID, title, pos, size, style)
    self.PostCreate(pre)

    # Now continue with the normal construction of the dialog
    padding = 4
    section_padding=60

    mainBox = wx.BoxSizer(wx.VERTICAL)
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((8, 8))

    #Video File
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.Add(wx.StaticText(self, -1, "Video File"), 0, wx.EXPAND|wx.ALL, 16)
  
    self.btnVideoFile = wx.Button(self, size=(380, -1))
    self.btnVideoFile.SetLabel('Select...')
    self.Bind(wx.EVT_BUTTON, self.OnClickVideoFile, self.btnVideoFile)
    hbox.Add(self.btnVideoFile, 0, wx.EXPAND|wx.ALL, padding)

    #Quality
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)

    label = wx.StaticText(self, -1, "Video")
    hbox.AddSpacer((12, 10))
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((section_padding, 10))

    label = wx.StaticText(self, -1, "Quality:")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.videoquality = wx.TextCtrl(self, -1, '5.0', size=(32,-1))
    hbox.Add(self.videoquality, 0, wx.EXPAND|wx.ALL, padding)
    label = wx.StaticText(self, -1, "Bitrate (kbps):")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.videobitrate = wx.TextCtrl(self, -1, '', size=(65,-1))
    hbox.Add(self.videobitrate, 0, wx.EXPAND|wx.ALL, padding)
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((section_padding, 10))
       
    #Size
    box=45
    label = wx.StaticText(self, -1, "Size:")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.width = wx.TextCtrl(self, -1, '', size=(65,-1))
    hbox.Add(self.width, 0, wx.EXPAND|wx.ALL, padding)
    label = wx.StaticText(self, -1, "x")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.height = wx.TextCtrl(self, -1, '', size=(65,-1))
    hbox.Add(self.height, 0, wx.EXPAND|wx.ALL, padding)

    #Framerate
    label = wx.StaticText(self, -1, "Framerate:")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.framerate = wx.TextCtrl(self, -1, '', size=(40,-1))
    hbox.Add(self.framerate, 0, wx.EXPAND|wx.ALL, padding)

    #Crop
    box=35
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((section_padding, 10))
    label = wx.StaticText(self, -1, "Crop:")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)

    label = wx.StaticText(self, -1, "Top")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.cropTop = wx.TextCtrl(self, -1, '', size=(box,-1))
    hbox.Add(self.cropTop, 0, wx.EXPAND|wx.ALL, padding)

    label = wx.StaticText(self, -1, "Left")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.cropLeft = wx.TextCtrl(self, -1, '', size=(box,-1))
    hbox.Add(self.cropLeft, 0, wx.EXPAND|wx.ALL, padding)

    label = wx.StaticText(self, -1, "Bottom")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.cropBottom = wx.TextCtrl(self, -1, '', size=(box,-1))
    hbox.Add(self.cropBottom, 0, wx.EXPAND|wx.ALL, padding)

    label = wx.StaticText(self, -1, "Right")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.cropRight = wx.TextCtrl(self, -1, '', size=(box,-1))
    hbox.Add(self.cropRight, 0, wx.EXPAND|wx.ALL, padding)

    box=45

    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    label = wx.StaticText(self, -1, "Audio")
    hbox.AddSpacer((12, 10))
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)

    #Quality & Bitrate
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((section_padding, 10))
    label = wx.StaticText(self, -1, "Quality:")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.audioquality = wx.TextCtrl(self, -1, '1.0', size=(32,-1))
    hbox.Add(self.audioquality, 0, wx.EXPAND|wx.ALL, padding)

    label = wx.StaticText(self, -1, "Bitrate (kbps):")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.audiobitrate = wx.TextCtrl(self, -1, '', size=(box,-1))
    hbox.Add(self.audiobitrate, 0, wx.EXPAND|wx.ALL, padding)

    #Samplerate / Channels
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((section_padding, 10))
    label = wx.StaticText(self, -1, "Samplerate (Hz)")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.samplerate = wx.TextCtrl(self, -1, '', size=(56,-1))
    hbox.Add(self.samplerate, 0, wx.EXPAND|wx.ALL, padding)
    label = wx.StaticText(self, -1, "Channels")
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.channels = wx.TextCtrl(self, -1, '', size=(24,-1))
    hbox.Add(self.channels, 0, wx.EXPAND|wx.ALL, padding)

    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)

    '''
    #Metadata
    label = wx.StaticText(self, -1, "Metadata")
    hbox.AddSpacer((12, 10))
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)

    mbox=180
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    labels = wx.BoxSizer(wx.VERTICAL)
    inputs = wx.BoxSizer(wx.VERTICAL)
    hbox.AddSpacer((section_padding, 10))
    hbox.Add(labels, 0, wx.ALIGN_RIGHT|wx.EXPAND|wx.ALL)
    hbox.Add(inputs,0, wx.ALIGN_LEFT|wx.EXPAND|wx.ALL)

    #Title
    label = wx.StaticText(self, -1, "Title")
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.title = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.title, 0, wx.EXPAND|wx.ALL)

    #Artist
    label = wx.StaticText(self, -1, "Artist")
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.artist = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.artist, 0, wx.EXPAND|wx.ALL)

    #date
    label = wx.StaticText(self, -1, "Date", size=(mbox,-1))
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.date = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.date, 0, wx.EXPAND|wx.ALL)

    #Location
    label = wx.StaticText(self, -1, "Location")
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.location = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.location, 0, wx.EXPAND|wx.ALL)

    #Organization
    label = wx.StaticText(self, -1, "Organization")
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.organization = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.organization, 0, wx.EXPAND|wx.ALL)

    #Copyright
    label = wx.StaticText(self, -1, "Copyright")
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.copyright = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.copyright, 0, wx.EXPAND|wx.ALL)

    #License
    label = wx.StaticText(self, -1, "License")
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.license = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.license, 0, wx.EXPAND|wx.ALL)

    #Contact
    label = wx.StaticText(self, -1, "Contact")
    labels.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    self.contact = wx.TextCtrl(self, -1, '', size=(mbox,-1))
    inputs.Add(self.contact, 0, wx.EXPAND|wx.ALL)
    '''

    #Buttons
    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((8, 16))

    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((280, 10))
    self.btnCancel = wx.Button(self, wx.ID_CANCEL)
    self.btnCancel.SetLabel('Cancel')
    hbox.Add(self.btnCancel, 0, wx.EXPAND|wx.ALL, padding)

    self.btnOK = wx.Button(self, wx.ID_OK)
    self.btnOK.SetDefault()
    self.btnOK.Disable()
    self.btnOK.SetLabel('Encode')
    hbox.Add(self.btnOK, 0, wx.EXPAND|wx.ALL, padding)

    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((8, 8))
        

    self.SetSizerAndFit(mainBox)
    
    if parent.inputFile and os.path.exists(parent.inputFile):
      self.selectVideoFile(parent.inputFile)
    parent.inputFile = None
    
  def OnClickVideoFile(self, event):
    #transcoding later...
    wildcard = "Video files|*.OGG;*.ogg;*.OGV;*.ogv;*.AVI;*.avi;*.mov;*.MOV;*.dv;*.DV;*.mp4;*.MP4;*.m4v;*.mpg;*.mpeg;*.wmv;*.MPG;*.flv;*.FLV|All Files (*.*)|*.*"
    dialogOptions = dict()
    dialogOptions['message'] = 'Add Video..'
    dialogOptions['wildcard'] = wildcard
    dialog = wx.FileDialog(self, **dialogOptions)
    if dialog.ShowModal() == wx.ID_OK:
      filename = dialog.GetFilename()
      dirname = dialog.GetDirectory()
      self.selectVideoFile(os.path.join(dirname, filename))
    else:
      filename=None
    dialog.Destroy()
    return filename
  
  def selectVideoFile(self, videoFile):
        self.videoFile = videoFile
        lValue = videoFile
        lLenght = 45
        if len(lValue) > lLenght:
          lValue = "..." + lValue[-lLenght:]
        self.btnVideoFile.SetLabel(lValue)
        self.btnOK.Enable()

def addVideoDialog(parent):
  dlg = AddVideoDialog(parent, -1, "Add Video", size=(490, 560), style=wx.DEFAULT_DIALOG_STYLE)
  dlg.CenterOnScreen()
  val = dlg.ShowModal()
  result = dict()
  if val == wx.ID_OK:
    result['ok'] = True
    result['videoFile'] = dlg.videoFile
    for key in ('width', 'height', 'videoquality', 'videobitrate', 'framerate',
                'audioquality', 'audiobitrate', 'samplerate'):
      result[key] = getattr(dlg, key).GetValue()
    print result
  else:
    result['ok'] = False
  dlg.Destroy()
  return result
