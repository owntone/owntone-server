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


var g_req;
var baseUrl = window.location.toString();
baseUrl = baseUrl.substr(0,baseUrl.length-13);

function createRequest() {
    // All browsers but IE
    g_req = false;
    if (window.XMLHttpRequest) {
        g_req = new XMLHttpRequest();
    // MS IE
    } else if (window.ActiveXObject) {
        g_req = new ActiveXObject("Microsoft.XMLHTTP");
    }
}
function init() {
  // get playlists
  createRequest();
  //alert(baseUrl+'databases/1/browse/genres?output=xml');
  g_req.open('get',baseUrl+'databases/1/containers?output=xml',false);
  g_req.send(null);
  el = document.getElementById('source');
  addPlaylists(el,g_req.responseXML);
  el.addEventListener('change',playlistSelect,true);
  
  g_req.open('get',baseUrl+'databases/1/browse/genres?output=xml',false);
  g_req.send(null);
  el = document.getElementById('genre');
  addOptions(el,g_req.responseXML);

  g_req.open('get',baseUrl+'databases/1/browse/artists?output=xml',false);
  g_req.send(null);
  el = document.getElementById('artist');
  addOptions(el,g_req.responseXML);

  g_req.open('get',baseUrl+'databases/1/browse/albums?output=xml',false);
  g_req.send(null);
  el = document.getElementById('album');
  addOptions(el,g_req.responseXML);

}
function addOptions(el,xmldoc) {
  items = xmldoc.getElementsByTagName('dmap.listingitem');
  for (i=0; i<items.length; i++) {
    option = document.createElement('option');
    option.setAttribute('value',items[i].firstChild.nodeValue);
    option.appendChild(document.createTextNode(items[i].textContent));
    el.appendChild(option);
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
    addRow(tableBody,song,time,artist,album,genre);
  }
  
}
function addRow(tbody,song,time,artist,album,genre) {
  row = document.createElement("tr");
  cell = document.createElement("td");
  cell.appendChild(document.createTextNode(song));
  row.appendChild(cell);

  cell = document.createElement("td");
  cell.appendChild(document.createTextNode(time));
  row.appendChild(cell);

  cell = document.createElement("td");
  cell.appendChild(document.createTextNode(artist));
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
  table.appendChild(tableBody);
  return tableBody;
}
