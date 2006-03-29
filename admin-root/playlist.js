// TODO
// Find a decent spinner instad of the busy text
// Handle 'all' in select boxes
// move stuff to responsehandler
// handle source change events
// navigate source with arrow keys and then click selected should initiate edit
// new playlist twice gives server response 500
// After playlist name edit, it should be activated again.
// After playlist delete, select another one

Event.observe(window,'load',initPlaylist);

var SEARCH_DELAY = 500; // # ms without typing before the search box searches
var BROWSE_TEXT_LEN = 30; // Length to truncate genre/artist/album select boxes
Ajax.Responders.register({  onCreate: function () {$('busymsg').style.visibility='visible';},
                          onComplete: function () {if (!Query.busy) {$('busymsg').style.visibility='hidden';}}});
function initPlaylist() {
  new Ajax.Request('/databases/1/containers?output=xml',{method: 'get',onComplete:rsSource});
  Query.send('genres');
  Event.observe('search','keypress',Search.keyPress);
  Event.observe('source','change',EventHandler.sourceChange);
  Event.observe('source','click',EventHandler.sourceClick);
  Event.observe('source','keypress',EventHandler.sourceKeyPress);
  Event.observe('genres','change',EventHandler.genresChange);
  Event.observe('artists','change',EventHandler.artistsChange);
  Event.observe('albums','change',EventHandler.albumsChange);
  
  Event.observe(document,'click',GlobalEvents.click);
  Event.observe('edit_playlist_name','keypress',EditPlaylistName.keyPress);
  // Firefox remebers the search box value on page reload
  Field.clear('search');
}
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
}

var Search = {
  timeOut: '',
  keyPress:  function (e) {
    if (this.timeOut) {
      window.clearTimeout(this.timeOut);
    }
    if (e.keyCode == Event.KEY_RETURN) {
      EventHandler.search();  
    } else {
      this.timeOut = window.setTimeout(EventHandler.search,SEARCH_DELAY);
    }
  }
};
var EditPlaylistName = {
  playlistId: '',
  playlistName: '',
  _getOptionElement: function (id) {
     return option = $A($('source').getElementsByTagName('option')).find(function (el) {
       return (el.value == (id || EditPlaylistName.playlistId));
     });
  },
  keyPress: function (e) {
    input = $('edit_playlist_name');  
    if (e.keyCode == Event.KEY_ESC) {
      EditPlaylistName.hide();
    }
    if (e.keyCode == Event.KEY_RETURN) {
      EditPlaylistName.save();
    }
  },
  add: function () {
    var url = '/databases/1/containers/add?output=xml';
    url += '&org.mt-daapd.playlist-type=0&dmap.itemname=untitled%20playlist';
    new Ajax.Request(url ,{method: 'get',onComplete:EditPlaylistName.responseAdd});  
  },
  remove: function () {
    if (window.confirm('Really delete playlist?')) {
      var url = '/databases/1/containers/del?output=xml';
      url += '&dmap.itemid=' + $('source').value;
      new Ajax.Request(url ,{method: 'get',onComplete:EditPlaylistName.response});
      var option = EditPlaylistName._getOptionElement($('source').value);
      Element.remove(option);
    } 
  },
  save: function () {
    input = $('edit_playlist_name');  
    var url = '/databases/1/containers/edit?output=xml';
    url += '&dmap.itemid=' + EditPlaylistName.playlistId;
    url += '&dmap.itemname=' + encodeURIComponent(input.value);
    new Ajax.Request(url ,{method: 'get',onComplete:EditPlaylistName.response});
    var option = EditPlaylistName._getOptionElement();
    option.text = input.value;
    EditPlaylistName.hide();
  },
  show: function () {
    input = $('edit_playlist_name');
    input.style.top = RicoUtil.toDocumentPosition(EditPlaylistName._getOptionElement()).y+ 'px';
    input.value = this.playlistName;
    input.style.display = 'block';
    Field.activate(input);
    GlobalEvents.addClickListener(this);
  },
  hide: function () {
    $('edit_playlist_name').style.display = 'none';
    EditPlaylistName.playlistId = '';
    GlobalEvents.removeClickListener(this);
  },
  response: function (request) {
    // Check that the save gave response 200 OK
  },
  responseAdd: function(request) {
    var status = Element.textContent(request.responseXML.getElementsByTagName('dmap.status')[0]);
    if ('200' != status) {
      alert('There is a playlist with that name, write some code to handle this');
      return;
    }
    EditPlaylistName.playlistId = Element.textContent(request.responseXML.getElementsByTagName('dmap.itemid')[0]);
    EditPlaylistName.playlistName = 'untitled playlist';
    var o = document.createElement('option');
    o.value = EditPlaylistName.playlistId;
    o.text = EditPlaylistName.playlistName;
    $('static_playlists').appendChild(o);
    $('source').value = EditPlaylistName.playlistId;
    EditPlaylistName.show();
    Query.setSource(EditPlaylistName.playlistId);
    Query.send('genres');
  },
  click: function (e) {
    var x = Event.pointerX(e);
    var y = Event.pointerY(e);
    var el = $('edit_playlist_name');
    var pos = RicoUtil.toViewportPosition(el);
    if ((x > pos.x) && (x < pos.x + el.offsetWidth) &&
        (y > pos.y) && (y < pos.y + el.offsetHeight)) {
      // Click was in input box  
      return;
    }
    if (EditPlaylistName.playlistId) {
      EditPlaylistName.save();
    }
  }  
}
var EventHandler = {
  sourceClickCount: [],
  sourceClick: function (e) {
    var playlistId = Event.element(e).value;
    if (1 == playlistId) {
      // do nothing for Library
      return;
    }
    if (EventHandler.sourceClickCount[playlistId]) {
      EventHandler.sourceClickCount[playlistId]++
    } else {
      EventHandler.sourceClickCount[playlistId] = 1;
    }
    if (EventHandler.sourceClickCount[playlistId] > 1) {
      el = Event.element(e);
      if (!el.text) {
        // Firefox generates and event when clicking in and empty area
        // of the select box
        return;  
      }
      EditPlaylistName.playlistId = el.value;
      EditPlaylistName.playlistName = el.text;
      EditPlaylistName.show();
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
      EditPlaylistName.remove();  
    }
    if (113 == e.keyCode) {
      // F2 
//TODO edit playist, what is the key on a mac?
    }
  },
  search: function () {
    Query.setSearchString($('search').value);
    Query.send('genres'); 
  },
  genresChange: function (e) {
    EventHandler.setSelected('genres');
    Query.send('artists');
  },
  artistsChange: function (e) {
    EventHandler.setSelected('artists');
    Query.send('albums');
  },
  albumsChange: function (e) {
    EventHandler.setSelected('albums');
    Query.send('songs');
  },
  setSelected: function (type) {
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
   baseUrl: '/databases/1/',
   playlistUrl: '',
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
    if (1 == playlistId) {
      Query.playlistUrl = '';
    } else {
      Query.playlistUrl = 'containers/' + playlistId + '/';
    }
   },
   getUrl: function (type) {
     var query=[];
     switch (type) {
       case 'artists':
         if (this.genres.length > 0) {
           query = this.genres.collect(function(value){return "'daap.songgenre:"+value.encode()+"'";});
         }
         break;
      case 'albums':
         if (this.artists.length > 0) {
           query = this.artists.collect(function(value){return "'daap.songartist:"+value.encode()+"'";});  
         } else if (this.genres.length > 0) {
           query = this.genres.collect(function(value){return "'daap.songgenre:"+value.encode()+"'";});    
         }
         break;
      case 'songs':
         if (this.albums.length > 0) {
           query = this.albums.collect(function(value){return "'daap.songalbum:"+value.encode()+"'";});  
         } else if (this.artists.length > 0) {
           query = this.artists.collect(function(value){return "'daap.songartist:"+value.encode()+"'";});  
         } else if (this.genres.length > 0) {
           query = this.genres.collect(function(value){return "'daap.songgenre:"+value.encode()+"'";});    
         }
         break;
      default:
         // Do nothing
         break;
     }
     if (this.searchString) {
       var search = [];
       var string = this.searchString.encode();
       ['daap.songgenre','daap.songartist','daap.songalbum','dmap.itemname'].each(function (item) {
         search.push("'" + item +':*' + string + "*'");  
       });
       if (query.length > 0) {
         return '&query=(' +search.join(',') + ')+('.encode() + query.join(',')+ ')';
       } else {
         return '&query=' + search.join(','); 
       }
     } else {
       if (query.length > 0) {
         return '&query=' + query.join(',');
       } else {
         return '';
       }
     }
   },
   send: function (type) {
     this.busy = true;
     var url;
     var handler;
     var meta = '';
     var index = '';
     switch (type) {
       case 'genres':
         url = 'browse/genres';
         handler = ResponseHandler.genreAlbumArtist;
         break;
       case 'artists':
         url = 'browse/artists';
         handler = ResponseHandler.genreAlbumArtist;
         break;
       case 'albums':
         url = 'browse/albums';
         handler = ResponseHandler.genreAlbumArtist;
         break;
       case 'songs':
         url = 'items';
         meta = '&meta=daap.songalbum,daap.songartist,daap.songgenre,dmap.itemid,daap.songtime,dmap.itemname';
         index = '&index=0-50';
         handler = rsSongs;
        break;
       default:
         alert("Shouldn't happen 2");
         break;
     }
     url = this.baseUrl + this.playlistUrl + url + '?output=xml' + index + meta + this.getUrl(type);
     new Ajax.Request(url ,{method: 'get',onComplete:handler});
   }
};
var ResponseHandler = {
  genreAlbumArtist: function (request) {
    var type;
    if (request.responseXML.getElementsByTagName('daap.browsegenrelisting').length > 0) {
      type = 'genres';
    }
    if (request.responseXML.getElementsByTagName('daap.browseartistlisting').length > 0) {
      type = 'artists';
    }
    if (request.responseXML.getElementsByTagName('daap.browsealbumlisting').length > 0) {
      type = 'albums';
    }
  
    var items = $A(request.responseXML.getElementsByTagName('dmap.listingitem'));
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
    if (addOptions(type,items,selected)) {
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
        Query.send('songs');
        break;
      default:
        alert("Shouldn't happen 3");
        break;  
    }
  }
};
function addOptions(type,options,selected) {
  el = $(type);
  var nothingSelected = true;
  options.each(function (option) {
    var node;
    //###FIXME I have no idea why the Builder.node can't create options
    // with the selected state I want.
    var o = document.createElement('option');
    o.value = option;
    var text = option.truncate(BROWSE_TEXT_LEN);
    if (option.length != text.length) {
      o.title = option;
      o.appendChild(document.createTextNode(text));
    } else {
      o.appendChild(document.createTextNode(option));
    }
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

function rsSongs(request) {
  var items = $A(request.responseXML.getElementsByTagName('dmap.listingitem'));
  var songsTable = $('songs_data');
  Element.removeChildren(songsTable);
  items.each(function (item) {
    var tr = document.createElement('tr');
    var time = parseInt(Element.textContent(item.getElementsByTagName('daap.songtime')[0]));
    time = Math.round(time / 1000);
    var seconds = time % 60;

    seconds = (seconds < 10) ? '0'+seconds : seconds;
    timeString =  Math.floor(time/60)+ ':' + seconds;

    tr.appendChild(Builder.node('td',{style:'width: 140px;'},Element.textContent(item.getElementsByTagName('dmap.itemname')[0])));
    tr.appendChild(Builder.node('td',{style:'width: 50px;'},timeString));
    tr.appendChild(Builder.node('td',{style:'width: 120px;'},Element.textContent(item.getElementsByTagName('daap.songartist')[0])));
    tr.appendChild(Builder.node('td',{style:'width: 120px;'},Element.textContent(item.getElementsByTagName('daap.songalbum')[0])));
    tr.appendChild(Builder.node('td',{style:'width: 100px;'},Element.textContent(item.getElementsByTagName('daap.songgenre')[0])));    

    songsTable.appendChild(tr);
  });
  Query.busy = false; 
}

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
function add() {
    alert('add');
}
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
