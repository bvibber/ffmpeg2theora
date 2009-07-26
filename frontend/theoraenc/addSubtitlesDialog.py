# -*- coding: utf-8 -*-
# vi:si:et:sw=2:sts=2:ts=2

import os
from os.path import basename
import time
import subprocess

import wx
#import wx.lib.langlistctrl
#from wx.lib.langlistctrl import GetWxIdentifierForLanguage
from wx.lib.mixins.listctrl import ListCtrlAutoWidthMixin

# on my box, I only get two versions of en, and two languages I'd never heard of, and keyboard input is unavailable
# to override for a language not in the list, so we don't use it, though it'd be nice as languages names would be
# translated, etc.
# After installing all locales, I don't even get some non obscure locales.
# And it doesn't seem to use ISO 639-1 tags anyway, but wxWidgets specific enums.
# Therefore, we use a "ComboBox" widget instead, and build a list of languages from a known set plus parsing
# the output of 'locale -a' to ensure we get the user's own language, if set (and also plenty of others if using
# a distro that spams the locale database with lots of unused ones); keyboard input overrides is available.
#use_langlistctrl=False

class SubtitlesProperties(wx.Dialog):
  def __init__(
          self, parent, ID, title,
          language, category, encoding, file, hasIconv,
          size=wx.DefaultSize, pos=wx.DefaultPosition, 
          style=wx.DEFAULT_DIALOG_STYLE,
          ):
    pre = wx.PreDialog()
    pre.Create(parent, ID, title, pos, size, style)
    self.PostCreate(pre)

    self.hasIconv = hasIconv

    # defaults
    if language == '':
      language = 'en'
    if category == '':
      category = 'SUB'
    if encoding == '':
      encoding = 'UTF-8'

    padding = 4

    mainBox = wx.BoxSizer(wx.VERTICAL)
    mainBox.AddSpacer((8, 16))

    # file
    self.btnSubtitlesFile = wx.Button(self, size=(380, -1))
    self.btnSubtitlesFile.SetLabel('Select...')
    self.Bind(wx.EVT_BUTTON, self.OnClickSubtitlesFile, self.btnSubtitlesFile)
    self.addProperty(mainBox, 'File', self.btnSubtitlesFile)

    # language
#    if use_langlistctrl:
#      self.languageWidget = wx.lib.langlistctrl.LanguageListCtrl(self, -1, style=wx.LC_REPORT, size=(380,140))
#    else:
#      self.languageWidget = wx.ComboBox(self, -1, language, (380,-1), wx.DefaultSize, self.BuildLanguagesList(), wx.CB_SIMPLE)
    self.languageWidget = wx.ComboBox(self, -1, language, (380,-1), wx.DefaultSize, self.BuildLanguagesList(), wx.CB_SIMPLE)
    self.addProperty(mainBox, 'Language', self.languageWidget, self.OnLanguageHelp)

    # category
    categories = ['SUB', 'CC', 'TRX', 'LRC'] # TODO: change when Silvia's list is final
    self.categoryWidget = wx.ComboBox(self, -1, category, (80,-1), wx.DefaultSize, categories, wx.CB_SIMPLE)
    self.addProperty(mainBox, 'Category', self.categoryWidget, self.OnCategoryHelp)

    # encoding
    if hasIconv:
      self.encodingWidget = wx.ComboBox(self, -1, encoding, (80,-1), wx.DefaultSize, self.BuildEncodingsList(self.hasIconv), wx.CB_SIMPLE)
    else:
      self.encodingWidget = wx.Choice(self, -1, (80,-1), choices=self.BuildEncodingsList(self.hasIconv))
    self.addProperty(mainBox, 'Encoding', self.encodingWidget, self.OnEncodingHelp)

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
    self.btnOK.SetLabel('OK')
    hbox.Add(self.btnOK, 0, wx.EXPAND|wx.ALL, padding)

    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox)
    hbox.AddSpacer((8, 8))

    self.SetSizerAndFit(mainBox)

    # preselect file, if any
    if file and file != '' and os.path.exists(file):
      self.selectSubtitlesFile(file)

  def addProperty(self, mainBox, name, widget, help=None):
    padding = 4
    vspacer = 40
    hspacer = 80

    hbox = wx.BoxSizer(wx.HORIZONTAL)
    mainBox.Add(hbox, 0, padding)
    hbox.AddSpacer((8, 8))
    label = wx.StaticText(self, -1, name)
    label.SetMinSize((hspacer,vspacer))
    hbox.Add(label, 0, wx.EXPAND|wx.ALL, padding)
    hbox.Add(widget, 0, padding)
    if help:
      hbox.AddSpacer((16, 0))
      btnHelp = wx.Button(self, size=(80, -1))
      btnHelp.SetLabel('More info...')
      self.Bind(wx.EVT_BUTTON, help, btnHelp)
      hbox.Add(btnHelp, 0, padding)
    hbox.AddSpacer((8, 8))

  def OnCategoryHelp(self, event):
    self.DisplayHelp(
      'The category is a string representing the semantics of the text in a Kate stream.\n'+
      'These codes include:\n'+
      '  SUB: text subtitles\n'+
      '  CC: closed captions\n'+
      '  TRX: transcript of a speech\n'+
      '  LRC: lyrics\n'+
      'If the category needed is not available in the list, a custom one may be entered.\n')

  def OnLanguageHelp(self, event):
    self.DisplayHelp(
      'Language is an ISO 639-1 or RFC 3066 language tag.\n'+
      'Usually, these are two letter language tags (eg, "en", or "de"), '+
      'optionally followed by a hypen (or underscore) and a country code (eg, "en_GB", "de_DE")\n'+
      'If the language tag needed is not available in the list, a custom one may be entered.\n')

  def OnEncodingHelp(self, event):
    iconv_blurb = ''
    if self.hasIconv:
      iconv_blurb = 'ffmpeg2theora was built with iconv support, so can also convert any encoding that is supported by iconv.\n'
    self.DisplayHelp(
      'Kate streams are encoded in UTF-8 (a Unicode character encoding that allows to represent '+
      'pretty much any existing script.\n'+
      'If the input file is not already encoded in UTF-8, it will need converting to UTF-8 first.\n'+
      'ffmpeg2theora can convert ISO-8859-1 (also known as latin1) encoding directly.\n'+
      iconv_blurb+
      'Files in other encodings will have to be converted manually in order to be used. See the '+
      'subtitles.txt documentation for more information on how to manually convert files.\n')

  def DisplayHelp(self, msg):
    wx.MessageBox(msg, 'More info...', style=wx.OK|wx.CENTRE)

  def OnClickSubtitlesFile(self, event):
    wildcard = "SubRip files|*.SRT;*.srt|All Files (*.*)|*.*"
    dialogOptions = dict()
    dialogOptions['message'] = 'Add subtitles..'
    dialogOptions['wildcard'] = wildcard
    dialog = wx.FileDialog(self, **dialogOptions)
    if dialog.ShowModal() == wx.ID_OK:
      filename = dialog.GetFilename()
      dirname = dialog.GetDirectory()
      self.selectSubtitlesFile(os.path.join(dirname, filename))
    else:
      filename=None
    dialog.Destroy()
    return filename

  def selectSubtitlesFile(self, subtitlesFile):
      self.subtitlesFile = subtitlesFile
      lValue = subtitlesFile
      lLength = 45
      if len(lValue) > lLength:
        lValue = "..." + lValue[-lLength:]
      self.btnSubtitlesFile.SetLabel(lValue)
      self.btnOK.Enable()

  def BuildLanguagesList(self):
    # start with a known basic set
    languages = ['en', 'ja', 'de', 'fr', 'it', 'es', 'cy', 'ar', 'cn', 'pt', 'ru']
    # add in whatever's known from 'locale -a' - this works fine if locale isn't found,
    # but i'm not sure what that'll do if we get another program named locale that spews
    # random stuff to stdout :)
    p = subprocess.Popen(['locale', '-a'], shell=False, stdout=subprocess.PIPE, close_fds=True)
    data, err = p.communicate()

    for line in data.strip().split('\n'):
      line = self.ExtractLanguage(line)
      if line != '' and line != 'C' and line != 'POSIX' and line not in languages:
        languages.append(line)
    languages.sort()
    return languages

  def ExtractLanguage(self, line):
    line = line.split('.')[0] # stop at a dot
    line = line.split(' ')[0] # stop at a space
    line = line.split('@')[0] # stop at a @
    line = line.split('\t')[0] # stop at a tab
    line = line.split('\n')[0] # stop at a newline
    line = line.split('\r')[0] # Mac or Windows
    return line

  def BuildEncodingsList(self, hasIconv):
    # start with a known basic set, that ffmpeg2theora can handle without iconv
    encodings = ['UTF-8', 'ISO-8859-1']

    # this creates a *huge* spammy list with my version of iconv...
    if hasIconv:
      # add in whatever iconv knows about
      p = subprocess.Popen(['iconv', '-l'], shell=False, stdout=subprocess.PIPE, close_fds=True)
      data, stderr = p.communicate()
      for line in data.strip().split('\n'):
        line = line.split('/')[0] # stop at a /
        if not line in encodings:
          encodings.append(line)
    return encodings

def addSubtitlesPropertiesDialog(parent, language, category, encoding, file, hasIconv):
  dlg = SubtitlesProperties(parent, -1, "Add subtitles", language, category, encoding, file, hasIconv, size=(490, 560), style=wx.DEFAULT_DIALOG_STYLE)
  dlg.CenterOnScreen()
  val = dlg.ShowModal()
  result = dict()
  if val == wx.ID_OK:
    result['ok'] = True
    result['subtitlesFile'] = dlg.subtitlesFile
#    if use_langlistctrl:
#      result['subtitlesLanguage'] = GetWxIdentifierForLanguage(dlg.languageWidget.GetLanguage())
#    else:
#      result['subtitlesLanguage'] = dlg.languageWidget.GetValue()
    result['subtitlesLanguage'] = dlg.languageWidget.GetValue()
    result['subtitlesCategory'] = dlg.categoryWidget.GetValue()
    if hasIconv:
      result['subtitlesEncoding'] = dlg.encodingWidget.GetValue()
    else:
      result['subtitlesEncoding'] = dlg.encodingWidget.GetStringSelection()
    print result
  else:
    result['ok'] = False
  dlg.Destroy()
  return result


class SubtitlesList(wx.ListCtrl, ListCtrlAutoWidthMixin):
  def __init__(self, parent):
    wx.ListCtrl.__init__(self, parent, -1, style=wx.LC_REPORT)
    ListCtrlAutoWidthMixin.__init__(self)

    self.ClearAll()
    self.InsertColumn(0, "Language")
    self.InsertColumn(1, "Category")
    self.InsertColumn(2, "Encoding")
    self.InsertColumn(3, "Name")
    self.SetColumnWidth(0, 80)
    self.SetColumnWidth(1, 80)
    self.SetColumnWidth(2, 80)
    self.SetColumnWidth(3, 80)

  def ResizeFilenameColumn(self):
    if self.GetItemCount() > 0:
      self.resizeLastColumn(1024)
    else:
      self.resizeLastColumn(0)

