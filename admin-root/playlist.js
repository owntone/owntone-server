// TODO
// move stuff to responsehandler
// handle source change events (keyPress etc)
// If playlist is empty don't confirm delete
Event.observe(window,'load',initPlaylist);

var SEARCH_DELAY = 500; // # ms without typing before the search box searches
var BROWSE_TEXT_LEN = 30; // genres/artists/albums select options longer than this will have
                          // a title attribute (to show a tool tip for items that doesn't fit the box
var g_myLiveGrid; // the live grid;
function initPlaylist() {
Ajax.Responders.register({  onCreate: Spinner.incRequestCount,
                          onComplete: Spinner.decRequestCount});

  new Ajax.Request('/databases/1/containers?output=xml',{method: 'get',onComplete:rsSource});
  Query.send('genres');
  Event.observe('search','keypress',EventHandler.searchKeyPress);
  Event.observe('source','change',EventHandler.sourceChange);
  Event.observe('source','click',EventHandler.sourceClick);
  Event.observe('source','keypress',EventHandler.sourceKeyPress);
  Event.observe('genres','change',EventHandler.genresChange);
  Event.observe('artists','change',EventHandler.artistsChange);
  Event.observe('albums','change',EventHandler.albumsChange);
  Event.observe('add_playlist_href','click',EventHandler.addPlaylistHrefClick);
  
  Event.observe(document,'click',GlobalEvents.click);
  Event.observe('edit_playlist_name','keypress',EventHandler.editPlaylistNameKeyPress);
  Event.observe('songs_data','click',SelectedRows.click);
  // Firefox remebers the search box value on page reload
  Field.clear('search');
  g_myLiveGrid = new Rico.LiveGrid('songs_data',20,1000,'',{prefetchBuffer: false});
}
var Spinner = {
  count: 0,
  incRequestCount: function (ca) {
    Spinner.count++;
    $('spinner').style.visibility = 'visible';
  },
  decRequestCount: function (caller) {
    Spinner.count--;
    if (/index/.test(caller.url)) {
      Spinner.count = 0;
      $('spinner').style.visibility = 'hidden';
    }
  }  
};
var GlobalEvents = {
  _clickListeners: [],
  click: function (e) {
    GlobalEvents._clickListeners.each(function (name) {
      name.click(e);  
    });  
  },
  addClickListener: function (el) {
    this._clickListeners.push(el);  
  },
  removeClickListener: function (el) {
    this._clickListeners = this._clickListeners.findAll(function (element) {
      return (element != el);
    });
  }
};

var Source = {
  playlistId: '',
  playlistName: '',
  _getOptionElement: function (id) {
     return option = $A($('source').getElementsByTagName('option')).find(function (el) {
       return (el.value == id);
     });
  },
  addPlaylist: function () {
    var url = '/databases/1/containers/add?output=xml';
    var name= 'untitled playlist';
    if (this._playlistExists(name)) {
      var i=1;
      while (this._playlistExists(name +' ' + i)) {
        i++;
      }
      name += ' ' +i;
    }
    this.playlistName = name;
    url += '&org.mt-daapd.playlist-type=0&dmap.itemname=' + encodeURIComponent(name);
    new Ajax.Request(url ,{method: 'get',onComplete:this.responseAdd});  
  },
  _playlistExists: function (name) {
     return $A($('source').getElementsByTagName('option')).pluck('firstChild').find(function (el) {
      return el.nodeValue == name;
    });
  },
  removePlaylist: function () {
    if (window.confirm('Really delete playlist?')) {
      var url = '/databases/1/containers/del?output=xml';
      url += '&dmap.itemid=' + $('source').value;
      new Ajax.Request(url ,{method: 'get',onComplete:this.response});
      var option = this._getOptionElement($('source').value);
      Element.remove(option);
    } 
  },
  savePlaylistName: function () {
    input = $('edit_playlist_name');  
    var url = '/databases/1/containers/edit?output=xml';
    url += '&dmap.itemid=' + Source.playlistId;
    url += '&dmap.itemname=' + encodeURIComponent(input.value);
    new Ajax.Request(url ,{method: 'get',onComplete:this.response});
    var option = this._getOptionElement(Source.playlistId);
    option.text = input.value;
    this.hideEditPlaylistName();
  },
  editPlaylistName: function () {
    input = $('edit_playlist_name');
    Source.playlistId = $('source').value;
    playlistName = Element.textContent(this._getOptionElement(Source.playlistId));
    //###FIXME use prototype Position instead
    input.style.top = RicoUtil.toDocumentPosition(this._getOptionElement(Source.playlistId)).y + 'px';
    input.value = playlistName;
    input.style.display = 'block';
    Field.activate(input);
    GlobalEvents.addClickListener(this);
  },
  hideEditPlaylistName: function () {
    $('edit_playlist_name').style.display = 'none';
    Field.activate('source');
    Source.playlistId = '';
    GlobalEvents.removeClickListener(this);
  },
  response: function (request) {
    // Check that the save gave response 200 OK
  },
  responseAdd: function(request) {
    var status = Element.textContent(request.responseXML.getElementsByTagName('dmap.status')[0]);
    if ('200' != status) {
//###FIXME if someone else adds a playlist with the same name
// as mine, (before My page is refreshed) won't happen that often
      alert('There is a playlist with that name, write some code to handle this');
      return;
    }
    Source.playlistId = Element.textContent(request.responseXML.getElementsByTagName('dmap.itemid')[0]);
    var o = document.createElement('option');
    o.value = Source.playlistId;
    o.text = Source.playlistName;
    $('static_playlists').appendChild(o);
    $('source').value = Source.playlistId;
    Source.editPlaylistName();
    Query.setSource(Source.playlistId);
    Query.send('genres');
  },
  click: function (e) {
    //###FIXME use prototype Position instead
    var x = Event.pointerX(e);
    var y = Event.pointerY(e);
    var el = $('edit_playlist_name');
    var pos = RicoUtil.toViewportPosition(el);
    if ((x > pos.x) && (x < pos.x + el.offsetWidth) &&
        (y > pos.y) && (y < pos.y + el.offsetHeight)) {
      // Click was in input box  
      return;
    }
    Source.savePlaylistName();
  }  
};

var EventHandler = {
  searchTimeOut: '',
  sourceClickCount: [],
  sourceClick: function (e) {
    var playlistId = Event.element(e).value;
    if (1 == playlistId) {
      // do nothing for Library
      return;
    }
    if (EventHandler.sourceClickCount[playlistId]) {
      EventHandler.sourceClickCount[playlistId]++;
    } else {
      EventHandler.sourceClickCount[playlistId] = 1;
    }
    if (EventHandler.sourceClickCount[playlistId] > 1) {
      el = Event.element(e);
      if (!document.all && !el.text) {
        // IE sends the select in the event, not the option
        // Firefox generates and event when clicking in and empty area
        // of the select box
        return;  
      }
      Source.editPlaylistName();
    }
  },
  sourceChange: function (e) {
    EventHandler.sourceClickCount = [];
    Field.clear('search');    
    var playlistId = $('source').value;
    Query.setSource(playlistId);
    Query.send('genres');
  },
  sourceKeyPress: function (e) {
    if (e.keyCode == Event.KEY_DELETE) {
      Source.removePlaylist();  
    }
    if (113 == e.keyCode) {
      // F2 
//TODO edit playist, what is the key on a mac?
    }
  },
  editPlaylistNameKeyPress: function (e) {
    input = $('edit_playlist_name');  
    if (e.keyCode == Event.KEY_ESC) {
      Source.hideEditPlaylistName();
    }
    if (e.keyCode == Event.KEY_RETURN) {
      Source.savePlaylistName();
    }
  },
  addPlaylistHrefClick: function (e) {
    Source.addPlaylist();  
  },
  searchKeyPress: function (e) {
    if (EventHandler.searchTimeOut) {
      window.clearTimeout(EventHandler.searchTimeOut);
    }
    if (e.keyCode == Event.KEY_RETURN) {
      EventHandler._search();  
    } else {
      EventHandler.searchTimeOut = window.setTimeout(EventHandler._search,SEARCH_DELAY);
    }
  },
  _search: function () {
    Query.setSearchString($('search').value);
    Query.send('genres'); 
  },
  genresChange: function (e) {
    EventHandler._setSelected('genres');
    Query.send('artists');
  },
  artistsChange: function (e) {
    EventHandler._setSelected('artists');
    Query.send('albums');
  },
  albumsChange: function (e) {
    EventHandler._setSelected('albums');
    SelectedRows.clearAll();
    g_myLiveGrid.resetContents();
    g_myLiveGrid.requestContentRefresh(0);
//    Query.send('songs');
  },
  _setSelected: function (type) {
    var options = $A($(type).options);
    Query.clearSelection(type);
    if ($(type).value != 'all') {
      options.each(function (option) {
        if (option.selected) {
           Query.addSelection(type,option.value);
        }
      });
    }
  }
};

var Query = {
   baseUrl: '/rsp/db/',
   playlistId: '1',
   genres: [],
   artists:[],
   albums: [],
   busy: '',
   searchString: '',
   clearSelection: function (type) {
     this[type] = [];
   },
   addSelection: function (type,value){
     this[type].push(value);
   },
   setSearchString: function (string) {
     this.searchString = string;  
   },
   setSource: function (playlistId) {
    Query.clearSelection('genres');
    Query.clearSelection('artists');
    Query.clearSelection('albums');
    Query.setSearchString('');
    Query.playlistId = playlistId;
   },
   getUrl: function (type) {
     var query=[];
     switch (type) {
       case 'artists':
         if (this.genres.length > 0) {
           query = this.genres.collect(function(value){return 'genre%3D"'+value.encode()+'"';});
         }
         break;
      case 'albums':
         if (this.artists.length > 0) {
           query = this.artists.collect(function(value){return 'artist%3D"'+value.encode()+'"';});  
         } else if (this.genres.length > 0) {
           query = this.genres.collect(function(value){return 'genre%3D"'+value.encode()+'"';});    
         }
         break;
      case 'songs':
         if (this.albums.length > 0) {
           query = this.albums.collect(function(value){return 'album%3D"'+value.encode()+'"';});  
         } else if (this.artists.length > 0) {
           query = this.artists.collect(function(value){return 'artist%3D"'+value.encode()+'"';});  
         } else if (this.genres.length > 0) {
           query = this.genres.collect(function(value){return 'genre%3D"'+value.encode()+'"';});    
         }
         break;
      default:
         // Do nothing
         break;
     }
     if (this.searchString) {
       var search = [];
       var string = this.searchString.encode();
       ['genre','artist','album','title'].each(function (item) {
         search.push(item +' includes "' + string + '"');  
       });
       if (query.length > 0) {
         return '&query=(' + search.join(' or ') + ') and ('.encode() + query.join(' or ')+ ')';
       } else {
         return '&query=' + search.join(' or '); 
       }
     } else {
       if (query.length > 0) {
         return '&query=' + query.join(' or ');
       } else {
         return '';
       }
     }
   },
   getFullUrl: function (type) {
     this.busy = true;
     var url;
     var handler;
     var meta = '';
     switch (type) {
       case 'genres':
         url = '/genre';
         break;
       case 'artists':
         url = '/artist';
         break;
       case 'albums':
         url = '/album';
         break;
       case 'songs':
         url = '';
         meta = '&type=browse';
        break;
       default:
         alert("Shouldn't happen 2");
         break;
     }
     return this.baseUrl + this.playlistId + url + '?dummy=' + meta + this.getUrl(type);
   },
   send: function(type) {
     if (('genres' == type) || ('artists' == type) || ('albums' == type)) {
       handler = ResponseHandler[type];
     } else {
       return;
     //  handler = rsSongs;  
     }
     new Ajax.Request(this.getFullUrl(type), {method: 'get',onComplete:handler});
   }
};
var ResponseHandler = {
  genres: function (request) {
    ResponseHandler._genreAlbumArtist(request,'genres');
  },
  artists: function (request) {
    ResponseHandler._genreAlbumArtist(request,'artists');
  },
  albums: function (request) {
    ResponseHandler._genreAlbumArtist(request,'albums');
  },
  _genreAlbumArtist: function (request,type) {
    var items = $A(request.responseXML.getElementsByTagName('item'));
    items = items.collect(function (el) {
      return Element.textContent(el);
    }).sort();
    var select = $(type);
    Element.removeChildren(select);

    var o = document.createElement('option');
    o.value = 'all';
    o.appendChild(document.createTextNode('All (' + items.length + ' ' + type + ')'));
    select.appendChild(o);
    var selected = {};
    Query[type].each(function(item) {
      selected[item] = true; 
    });
    Query.clearSelection(type); 
    if (ResponseHandler._addOptions(type,items,selected)) {
      select.value='all';
    }
    switch (type) {
      case 'genres':
        Query.send('artists');
        break;
      case 'artists':
        Query.send('albums');
        break;
      case 'albums':
        SelectedRows.clearAll();
        g_myLiveGrid.resetContents();
        g_myLiveGrid.requestContentRefresh(0);
//        Query.send('songs');
        break;
      default:
        alert("Shouldn't happen 3");
        break;  
    }
  },
  _addOptions: function (type,options,selected) {
    el = $(type);
    var nothingSelected = true;
    options.each(function (option) {
      var node;
      //###FIXME I have no idea why the Builder.node can't create options
      // with the selected state I want.
      var o = document.createElement('option');
      o.value = option;
      if (option.length >= BROWSE_TEXT_LEN) {
        o.title = option;
      }
      o.appendChild(document.createTextNode(option));
      if (selected[option]) {
        o.selected = true;
        nothingSelected = false;
        Query.addSelection(type,option);
      } else {
        o.selected = false;
      }
      el.appendChild(o);
    });
    return nothingSelected;
  }
};

function rsSource(request) {
  var items = $A(request.responseXML.getElementsByTagName('dmap.listingitem'));
  var sourceSelect = $('source');
  var smartPlaylists = [];
  var staticPlaylists = [];
  Element.removeChildren(sourceSelect);

  items.each(function (item,index) {
    if (0 === index) {
      // Skip Library
      return;
    }  
  
    if (item.getElementsByTagName('com.apple.itunes.smart-playlist').length > 0) {
      smartPlaylists.push({name: Element.textContent(item.getElementsByTagName('dmap.itemname')[0]),
                             id: Element.textContent(item.getElementsByTagName('dmap.itemid')[0])});  
    } else {
      staticPlaylists.push({name: Element.textContent(item.getElementsByTagName('dmap.itemname')[0]),
                              id: Element.textContent(item.getElementsByTagName('dmap.itemid')[0])});
    }
  });
  sourceSelect.appendChild(Builder.node('option',{value: '1'},'Library'));
  if (smartPlaylists.length > 0) {
    optgroup = Builder.node('optgroup',{label: 'Smart playlists'});
    smartPlaylists.each(function (item) {
      var option = document.createElement('option');
      optgroup.appendChild(Builder.node('option',{value: item.id},item.name));
    });
    sourceSelect.appendChild(optgroup);
  }
  if (staticPlaylists.length > 0) {
    optgroup = Builder.node('optgroup',{label: 'Static playlists',id: 'static_playlists'});
    staticPlaylists.each(function (item) {
      var option = document.createElement('option');
      optgroup.appendChild(Builder.node('option',{value: item.id},item.name));
    });
    sourceSelect.appendChild(optgroup);
  }
  // Select Library
  sourceSelect.value = 1;
}

SelectedRows = {
  songId: [],
  click: function(e) {
    var tr = Event.findElement(e,'tr');
    var id = tr.getAttribute('songid');
    if (!id) {
      return;
    }
    if (e.ctrlKey) {
      if (SelectedRows.isSelected(tr)) {
        SelectedRows.unsetSelected(tr);
      } else {
        SelectedRows.setSelected(tr);
      }
      return;
    }
    if (e.shiftKey) {
      return;
    }
    if (SelectedRows.isSelected(tr)) {
      SelectedRows.clearAll();
    } else {
      SelectedRows.clearAll();
      SelectedRows.setSelected(tr);
    }
  },
  isSelected: function (tr) {
    return SelectedRows.songId[tr.getAttribute('songid')];
  },
  setSelected: function (tr) {
    SelectedRows.songId[tr.getAttribute('songid')] = tr.getAttribute('index');
    tr.style.backgroundColor = '#8CACBB';
  },
  unsetSelected: function (tr) {
    SelectedRows.songId[tr.getAttribute('songid')] = '';
    tr.style.backgroundColor = '';
  },
  clearAll: function () {
    SelectedRows.songId = [];
    $A($('songs_data').getElementsByTagName('tr')).each(SelectedRows.unsetSelected);
 },
  updateState: function (tr,songId,index) {
    if (songId && index) {
      tr.setAttribute('songid',songId);
      tr.setAttribute('index',index);
      if (SelectedRows.isSelected(tr)) {
        SelectedRows.setSelected(tr);
      } else {
        SelectedRows.unsetSelected(tr);  
      }
    } else {
      tr.removeAttribute('songid');
      tr.removeAttribute('index');
      SelectedRows.unsetSelected(tr);  
    }
  }
};

Object.extend(Element, {
  removeChildren: function(element) {
  while(element.hasChildNodes()) {
    element.removeChild(element.firstChild);
  }
  },
  textContent: function(node) {
  if ((!node) || !node.hasChildNodes()) {
    // Empty text node
    return '';
  } else {
    if (node.textContent) {
    // W3C ?
    return node.textContent;
    } else if (node.text) {
    // IE
    return node.text;
    }
  }
  // We shouldn't end up here;
  return '';
  }
});
String.prototype.encode = function () {
  return encodeURIComponent(this).replace(/\'/g,"\\'");
};
// Stolen from prototype 1.5
String.prototype.truncate = function(length, truncation) {
  length = length || 30;
  truncation = truncation === undefined ? '...' : truncation;
  var ret = (this.length > length) ? this.slice(0, length - truncation.length) + truncation : this;
  return '' + ret;
};
/* Detta script finns att hamta pa http://www.jojoxx.net och 
   far anvandas fritt sa lange som dessa rader star kvar. */ 
function DataDumper(obj,n,prefix){
	var str=""; prefix=(prefix)?prefix:""; n=(n)?n+1:1; var ind=""; for(var i=0;i<n;i++){ ind+="  "; }
	if(typeof(obj)=="string"){
		str+=ind+prefix+"String:\""+obj+"\"\n";
	} else if(typeof(obj)=="number"){
		str+=ind+prefix+"Number:"+obj+"\n";
	} else if(typeof(obj)=="function"){
		str+=ind+prefix+"Function:"+obj+"\n";
	} else if(typeof(obj) == 'boolean') {
       str+=ind+prefix+"Boolean:" + obj + "\n";
	} else {
		var type="Array";
		for(var i in obj){ type=(type=="Array"&&i==parseInt(i))?"Array":"Object"; }
		str+=ind+prefix+type+"[\n";
		if(type=="Array"){
			for(var i in obj){ str+=DataDumper(obj[i],n,i+"=>"); }
		} else {
			for(var i in obj){ str+=DataDumper(obj[i],n,i+"=>"); }
		}
		str+=ind+"]\n";
	}
	return str;
}
