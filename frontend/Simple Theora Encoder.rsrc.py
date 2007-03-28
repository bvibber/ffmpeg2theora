{'application':{'type':'Application',
          'name':'SimpleTheoraEncoder',
    'backgrounds': [
    {'type':'Background',
          'name':'bgTemplate',
          'title':'Simple Theora Encoder',
          'size':(559, 260),
          'style':['resizeable'],

        'menubar': {'type':'MenuBar',
         'menus': [
             {'type':'Menu',
             'name':'menuFile',
             'label':'&File',
             'items': [
                  {'type':'MenuItem',
                   'name':'menuFileExit',
                   'label':'E&xit',
                   'command':'exit',
                  },
              ]
             },
         ]
     },
         'components': [

{'type':'Button', 
    'name':'encodeQueue', 
    'position':(462, 187), 
    'enabled':False, 
    'label':u'Encode...', 
    },

{'type':'Button', 
    'name':'editItem', 
    'position':(461, 104), 
    'enabled':False, 
    'label':u'Edit...', 
    },

{'type':'Button', 
    'name':'removeItem', 
    'position':(461, 136), 
    'enabled':False, 
    'label':u'Remove', 
    },

{'type':'Button', 
    'name':'addItem', 
    'position':(462, 71), 
    'label':u'Add...', 
    },

{'type':'StaticText', 
    'name':'title', 
    'position':(15, 9), 
    'font':{'faceName': u'Lucida Grande', 'family': 'sansSerif', 'size': 28}, 
    'text':u'Simple Theora Encoder', 
    },

{'type':'MultiColumnList', 
    'name':'encodingQueue', 
    'position':(15, 49), 
    'size':(431, 164), 
    'backgroundColor':(255, 255, 255), 
    'columnHeadings':[], 
    'font':{'faceName': u'Lucida Grande', 'family': 'default', 'size': 12}, 
    'items':[], 
    'maxColumns':20, 
    'rules':1, 
    },

] # end components
} # end background
] # end backgrounds
} }
