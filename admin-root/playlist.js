Event.observe(window,'load',initPlaylist);
var MAX_SONGS= 400;
Ajax.Responders.register({  onCreate: function () {$('busymsg').style.visibility='visible';},
                          onComplete: function () {$('busymsg').style.visibility='hidden';}});
function initPlaylist() {
  new Ajax.Request('/databases/1/containers?output=xml',{method: 'get',onComplete:rsSource});
  Query.send('genres');
  Query.send('artists');
  Query.send('albums');

  Event.observe('source','change',sourceChange);
  Event.observe('genres','change',genresArtistsAlbumsChange);
  Event.observe('artists','change',genresArtistsAlbumsChange);
  Event.observe('albums','change',genresArtistsAlbumsChange);
}
var Query = {
   genres: new Array(),
   artists:new Array(),
   albums: new Array(),
   clearSelections: function (type) {
     this[type] = new Array();
     if ('genres' == type) {
       this.artists = new Array();
       this.albums = new Array();  
     }
     if ('artists' == type) {
       this.albums = new Array();  
     }
   },
   addSelection: function (type,value){
     this[type].push(value);
   },
   getUrl: function (type) {
     var query=new Array();
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
     }
     if (query.length > 0) {
       return '&query=' + query.join(',');
     } else {
       return ''; 
     }
   },
   send: function (type) {
     var url;
     var handler;
     var meta = '';
     switch (type) {
       case 'genres':
         url = '/databases/1/browse/genres';
         handler = rsGenresArtistsAlbums;
         break;
       case 'artists':
         url = '/databases/1/browse/artists';
         handler = rsGenresArtistsAlbums;
         break;
       case 'albums':
         url = '/databases/1/browse/albums';
         handler = rsGenresArtistsAlbums;
         break;
       case 'songs':
         url = '/databases/1/items';
         meta = '&meta=daap.songalbum,daap.songartist,daap.songgenre,dmap.itemid,daap.songtime,dmap.itemname';
         handler = rsSongs;
        break;
     }
     url = url + '?output=xml' + meta + this.getUrl(type);
     new Ajax.Request(url ,{method: 'get',onComplete:handler});
   }
};


function rsSource(request) {
  var items = $A(request.responseXML.getElementsByTagName('dmap.listingitem'));
  var sourceSelect = $('source');
  var smartPlaylists = new Array();
  var staticPlaylists = new Array();
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
    optgroup = Builder.node('optgroup',{label: 'Static playlists'});
    staticPlaylists.each(function (item) {
      var option = document.createElement('option');
      optgroup.appendChild(Builder.node('option',{value: item.id},item.name));
    });
    sourceSelect.appendChild(optgroup);
  }
  // Select Library
  sourceSelect.value = 1;
}

function genresArtistsAlbumsChange(e) {
  var type = Event.element(e).id;
  var options = $A($(type).options);
  Query.clearSelections(type);
  if ($(type).value != 'all') {
    options.each(function (option) {
      if (option.selected) {
         Query.addSelection(type,option.value);
      }
    });
  }
  switch (type) {
    case 'genres':
      Query.send('artists');
      Query.send('albums');
      Query.send('songs');
      break;
    case 'artists':
      Query.send('albums');
      Query.send('songs');
      break;
    case 'albums':
      Query.send('songs');
      break;
  }
}
function rsGenresArtistsAlbums(request) {
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
  addOptions(select,items);
  select.value='all';
}

function rsSongs(request) {
  var items = $A(request.responseXML.getElementsByTagName('dmap.listingitem'));
  var songsTable = $('songs_data');
  Element.removeChildren(songsTable);
  if (items.length > MAX_SONGS) {
    var tr = document.createElement('tr');
    tr.appendChild(Builder.node('td',{colspan: '5'},'Search returned > '+MAX_SONGS+' results'));
    songsTable.appendChild(tr);
    return;
  }
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
}
function addOptions(element,options) {
  options.each(function (option) {
    var node;
    var text = option.truncate(25);
    if (option.length != text.length) {
      node = Builder.node('option',{value: option, title: option},option.truncate(25));
    } else {
      node = Builder.node('option',{value: option},option.truncate(25));
    }
    node.selected = false;
    element.appendChild(node);
  });
}
function sourceChange(e) {
  alert('Playlist id:'+$('source').value);  
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
  return encodeURIComponent(this).replace(/\'/,"\\'");
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
		for(var i in obj){ type=(type=="Array"&&i==parseInt(i))?"Array":"Object" }
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
