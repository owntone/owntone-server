/*
 * $Id$
 * Javascript for playlist.html
 *
 * Copyright (C) 2005 Anders Betnér (andbe611@users.sourceforge.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* Detta script finns att hämta på http://www.jojoxx.net och 
   får användas fritt så länge som dessa rader står kvar. */ 

function DataDumper(obj,n,prefix){
	var str=""; prefix=(prefix)?prefix:""; n=(n)?n+1:1; var ind=""; for(var i=0;i<n;i++){ ind+="    "; }
	if(typeof(obj)=="string"){
		str+=ind+prefix+"String:\""+obj+"\"\n";
	} else if(typeof(obj)=="number"){
		str+=ind+prefix+"Number:"+obj+"\n";
	} else if(typeof(obj)=="function"){
		str+=ind+prefix+"Function:"+obj+"\n";
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

var g_req;
var g_requestor;
var g_selectedGenres = Array();
var g_selectedAlbums = Array();
var g_selectedArtists = Array();

var baseUrl = window.location.toString();
baseUrl = baseUrl.substr(0,baseUrl.length-13);
String.prototype.myEncodeURI = function () {
  s = encodeURI(this);
  s = s.replace(/\'/,'%27');
  return s.replace(/&/,'%26');
};

Requestor = function(baseUrl) {
  this.baseUrl = baseUrl;
  this.que = Array();
    // All browsers but IE
  g_req = false;
  if (window.XMLHttpRequest) {
    g_req = new XMLHttpRequest();
    // MS IE
  } else if (window.ActiveXObject) {
    g_req = new ActiveXObject("Microsoft.XMLHTTP");
  }
  this.busy = false;

};
Requestor.prototype.addRequest = function(url) {
  if (this.busy) {
    this.que.push(url);
  } else {
    this._openRequest(url);
  }
};

Requestor.prototype._openRequest = function(url) {
    this.busy = true;
    if (url.search(/\?/)> -1) {
      url += '&output=xml';
    } else {
      url += '?output=xml';
    }
    g_req.open('get',this.baseUrl+url,true);
    g_req.onreadystatechange = Requestor_handleRequest;
    g_req.send(null);
};
Requestor_handleRequest = function() {
  if (4   == g_req.readyState) {
    // readystate 4 means the whole document is loaded
    if (200 == g_req.status) {
      // Only try to parse the response if we got
      // ok from the server
      xmldoc = g_req.responseXML;
      if ('daap.databaseplaylists' == xmldoc.firstChild.nodeName) {
        el = document.getElementById('source');
        addPlaylists(el,xmldoc);
      } else if ('daap.databasebrowse' == xmldoc.firstChild.nodeName) {
        // Ok we have response from a browse query
      
        switch (xmldoc.firstChild.childNodes[3].nodeName) {
          case 'daap.browsegenrelisting':
                el = document.getElementById('genre');
                addOptions(el,'genre',xmldoc);
                break;
          case 'daap.browseartistlisting':
                el = document.getElementById('artist');
                addOptions(el,'artist',xmldoc);
                break;
          case 'daap.browsealbumlisting':
                el = document.getElementById('album');
                addOptions(el,'album',xmldoc);
                break;                    
          default:
                // We got something else back...
        }
      } else if ('daap.databasesongs' == xmldoc.firstChild.nodeName) {
        // Songlist
        addSongs(xmldoc);
      }
    }
    if (g_requestor.que.length > 0) {
      // Just to be able to pull ourseves up by our boot straps
      window.setTimeout(Requestor_queChecker,5);
      return;
    } else {
      g_requestor.busy = false;
    }
  }
};
function Requestor_queChecker() {
  if (url = g_requestor.que.shift()) {
    g_requestor._openRequest(url);
  }
}

function init() {
  g_requestor = new Requestor(baseUrl);
  g_requestor.addRequest('databases/1/containers');
  g_requestor.addRequest('databases/1/browse/genres');
  g_requestor.addRequest('databases/1/browse/artists');
  g_requestor.addRequest('databases/1/browse/albums');
  el = document.getElementById('source');
  el.addEventListener('change',playlistSelect,true);

  el = document.getElementById('genre');
  el.addEventListener('change',genreSelect,true);

  el = document.getElementById('artist');
  el.addEventListener('change',artistSelect,true);

//###FIXME album select
return;
  // get playlists
  
  g_req.open('get',baseUrl+'databases/1/containers?output=xml',false);
  g_req.send(null);
  
  
  g_req.open('get',baseUrl+'databases/1/browse/genres?output=xml',false);
  g_req.send(null);
    
  g_req.open('get',baseUrl+'databases/1/browse/artists?output=xml',false);
  g_req.send(null);
  
  g_req.open('get',baseUrl+'databases/1/browse/albums?output=xml',false);
  g_req.send(null);
  el = document.getElementById('album');
  addOptions(el,'album',g_req.responseXML);

}
function addOptions(el,label,xmldoc) {
  while(el.hasChildNodes()) {
    el.removeChild(el.firstChild);
  }
  itemCnt = xmldoc.getElementsByTagName('dmap.specifiedtotalcount').item(0).textContent;
  if (parseInt(itemCnt) > 1) {
    plural = 's';
  } else {
    plural = '';
  }
  option = document.createElement('option');
  option.value = '1';
  option.selected = false;
  option.appendChild(document.createTextNode('All ('+itemCnt+' '+label +plural+')'));
  el.appendChild(option);

  items = xmldoc.getElementsByTagName('dmap.listingitem');
  for (i=0; i<items.length; i++) {
    option = document.createElement('option');
    itemName = items[i].textContent;
    option.value = itemName;
    selectAll = true;
    switch (label) {
      case 'genre': 
        if (g_selectedGenres[itemName]) {
          //option.selected = true;
          selectAll = false;
        }
        break;
      case 'artist':
        if (g_selectedArtists[itemName]) {
          //option.selected = true;
          selectAll = false;
        }
      case 'album':
        if (g_selectedAlbums[itemName]) {
          //option.selected = true;
          selectAll = false;
        }
      default: // if we get here something is wrong
               // silently ignore it    
    }
    //###FIXME do some kind of itelligent truncation of the string
    option.appendChild(document.createTextNode(itemName.substr(0,30)));
    el.appendChild(option);
  }
  if (selectAll) {
    el.options[0].selected = true;
  } else {
    el.removeAttribute('selected');
  }
  if ('album' == label) {
    //### All other boxes are updated, this is the last one, now get the songs
    getSongs();
  }
}
function addPlaylists(el,xmldoc) {
  //items = xmldoc.getElementsByTagName('dmap.listingitem');
  list = xmldoc.childNodes[0].childNodes[4];
  // Start on 1 since the first node is the name of the daap server
  for (i=1; i < list.childNodes.length; i++) {
    option = document.createElement('option');
    listNumber = list.childNodes[i].childNodes[0].textContent;
    listName = list.childNodes[i].childNodes[2].textContent;
    option.appendChild(document.createTextNode(listName));
    option.setAttribute('value',listNumber);
    el.appendChild(option);
  }
}
function playlistSelect(event) {
  table = document.getElementById('songs');
  tableBody = removeRows(table);
  playlistNumber = document.getElementById('source').value;
  browse = document.getElementById('browse');
  if (1 == playlistNumber) {
    browse.style.display = 'block';
  } else {
    browse.style.display = 'none';
  }
  g_req.open('get',baseUrl + 'databases/1/containers/' + playlistNumber +
        '/items?meta=dmap.itemname,daap.songalbum,daap.songartist,daap.songgenre,daap.songtime&output=xml',false);
  g_req.send(null);
  
  items = g_req.responseXML.getElementsByTagName('dmap.listingitem');
  className = 'odd';
  for (i=0; i < items.length; i++) {
    // Have to check if the tag really was returned from the server
    if (song = items[i].getElementsByTagName('dmap.itemname').item(0)) {
      song = song.textContent;
    } else {
      song = '';
    }
    time = ''; //items[i].getElementsByTagName('daap.songtime').item(0).textContent;
    if (artist = items[i].getElementsByTagName('daap.songartist').item(0)) {
      artist = artist.textContent;
    } else {
      artist = '';
    }
    if (album = items[i].getElementsByTagName('daap.songalbum').item(0)) {
      album = album.textContent;
    } else {
      album = '';
    }
    if (genre = items[i].getElementsByTagName('daap.songgenre').item(0)) {
      genre = genre.textContent;
    } else {
      genre = '';
    }
    if ('odd' == className) {
      className = 'even';
    } else {
      className = 'odd';
    }
    addRow(tableBody,className,song,time,artist,album,genre);
  }
  
}
function addRow(tbody,className,song,time,artist,album,genre) {
  row = document.createElement("tr");
  row.setAttribute('class',className);
  cell = document.createElement("td");
  cell.appendChild(document.createTextNode(song));
  row.appendChild(cell);

  cell = document.createElement("td");
  cell.appendChild(document.createTextNode(time));
  row.appendChild(cell);

  cell = document.createElement("td");
  //###FIXME do some kind of itelligent truncation of the string
  cell.appendChild(document.createTextNode(artist.substring(0,30)));
  row.appendChild(cell);

  cell = document.createElement("td");
  cell.appendChild(document.createTextNode(album));
  row.appendChild(cell);
  
  cell = document.createElement("td");
  cell.appendChild(document.createTextNode(genre));
  row.appendChild(cell);
  tbody.appendChild(row);
}

function removeRows(table) {
  tableBody = table.getElementsByTagName('tbody').item(0);
  table.removeChild(tableBody);
  tableBody = document.createElement('tbody');
  tableBody.setAttribute('class','mytbody');
  table.appendChild(tableBody);
  return tableBody;
}
function genreSelect(event) {
  selectObject = event.target;
  if (selectObject.options[0].selected) {
    filter = '';
    g_selectedGenres = Array();
    //###FIXME must dselect all others
  } else {
    filter = Array();
    //###FIXME if there is just the all genre there will be an error here
    for (i=1; i<selectObject.options.length; i++) {
      genre = selectObject.options[i].value;
		  if (selectObject.options[i].selected) {
			  filter.push("'daap.songgenre:" + genre.myEncodeURI() + "'");
			  g_selectedGenres[genre] = true;
		  } else {
        g_selectedGenres[genre] = false;
      }
	  }
	  filter = '?filter=' + filter.join(',');
  }
  g_requestor.addRequest('databases/1/browse/artists' + filter);
  g_requestor.addRequest('databases/1/browse/albums' + filter);
}

function artistSelect(event) {
  selectObject = event.target;
  filter = '';
  if (selectObject.options[0].selected) {
    // All selected, sort according to genre
    filter = Array();
    for (genre in g_selectedGenres) {
      if (g_selectedGenres[genre]) {
        filter.push("'daap.songgenre:" + genre.myEncodeURI() + "'");
      }
    }
    if (0 == filter.length) {
      // If filter array is empty then "All genres" is selected
      // so set filter to empty
      filter = '';
    } else {
      filter = '?filter=' + filter.join(',');
    }
  } else {
    filter = Array();
    for (i=1; i<selectObject.options.length; i++) {
      artist = selectObject.options[i].value;
		  if (selectObject.options[i].selected) {
			  filter.push("'daap.songartist:" + artist.myEncodeURI() + "'");
			  g_selectedArtists[artist] = true;
		  } else {
        g_selectedArtists[artist] = false;
      }
	  }
	// If all is selected then search on all artists but honor genre
	  filter = '?filter=' + filter.join(',');
  }
	g_requestor.addRequest('databases/1/browse/albums' + filter);
}

function albumSelect(event) {
  // just get the songs, the only box changed is this one
  getSongs();
}
function getSongs() {
  genre = document.getElementById('genre').value;
  artist = document.getElementById('artist').value;
  album = document.getElementById('album').value;
  //alert("genre: "+genre +"\nartist: "+ artist + "\nalbum:"+ album);
  query = Array();
  if (genre != '1') {
    query.push('\'daap.songgenre:' + genre.myEncodeURI() +"'");
  }
  if (artist != '1') {
    query.push('\'daap.songartist:' + artist.myEncodeURI() +"'");
  }
  if (album != '1') {
    query.push('\'daap.songalbum:' + album.myEncodeURI() +"'");
  }
  if (0 == query.length) {
      // If filter array is empty then "All genres" is selected
      // so set filter to empty
    query = '';
  } else {
    query = '&query=' + query.join(',');
  }
  //alert(query);
  g_requestor.addRequest('databases/1/items' +
  '?meta=dmap.itemname,daap.songalbum,daap.songartist,daap.songgenre,daap.songtime'+query);
  ///items?meta=dmap.itemname,daap.songalbum,daap.songartist,daap.songgenre,daap.songtime&output=xml
  
}
function addSongs(xmldoc) {
  items = xmldoc.getElementsByTagName('dmap.listingitem');
  className = 'odd';
  table = document.getElementById('songs');
  tableBody = removeRows(table);

  for (i=0; i < items.length; i++) {
    // Have to check if the tag really was returned from the server
    if (song = items[i].getElementsByTagName('dmap.itemname').item(0)) {
      song = song.textContent;
    } else {
      song = '';
    }
    time = ''; //items[i].getElementsByTagName('daap.songtime').item(0).textContent;
    if (artist = items[i].getElementsByTagName('daap.songartist').item(0)) {
      artist = artist.textContent;
    } else {
      artist = '';
    }
    if (album = items[i].getElementsByTagName('daap.songalbum').item(0)) {
      album = album.textContent;
    } else {
      album = '';
    }
    if (genre = items[i].getElementsByTagName('daap.songgenre').item(0)) {
      genre = genre.textContent;
    } else {
      genre = '';
    }
    if ('odd' == className) {
      className = 'even';
    } else {
      className = 'odd';
    }
    addRow(tableBody,className,song,time,artist,album,genre);
  }

}
