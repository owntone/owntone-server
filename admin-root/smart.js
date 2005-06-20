var req;
var playlist_info={};

function selectPlaylist(e) {
    var targ;
    
    if(!e) var e=window.event;
    if(e.target) targ=e.target;
    else if (e.srcElement) targ=e.srcElement;
    if(targ.nodeType == 3)
        targ = targ.parentNode;
        
    while(targ.previousSibling)
        targ=targ.previousSibling;
    
    
    pl_id = targ.firstChild.nodeValue;


    alert(playlist_info[pl_id]['name']);   
}

function processPlaylists() {
    var xmldoc = req.responseXML;
    var playlists = xmldoc.getElementsByTagName("dmap.listingitem");
    var pl_table = document.getElementById("playlists");
    playlist_info = {};
    
    while(pl_table.childNodes.length > 0) {
        pl_table.removeChild(pl_table.lastChild);
    }
        
    for(var x=0; x < playlists.length; x++) {
        var pl_id=playlists[x].getElementsByTagName("dmap.itemid")[0].firstChild.nodeValue;
        var pl_name=playlists[x].getElementsByTagName("dmap.itemname")[0].firstChild.nodeValue;
        var pl_type=playlists[x].getElementsByTagName("org.mt-daapd.playlist-type")[0].firstChild.nodeValue;


        playlist_info[String(pl_id)] = { 'name': pl_name, 'type': pl_type };
        if(pl_type == 1) {
            var pl_spec=playlists[x].getElementsByTagName("org.mt-daapd.smart-playlist-spec")[0].firstChild.nodeValue;
            playlist_info[String(pl_id)]['spec'] = pl_spec;
        }
        
        switch(pl_type) {
            case "0":
                pl_type = "Static&nbsp;(Web&nbsp;Edited)";
                break;
            case "1":
                pl_type = "Smart";
                break;
            case "2":
                pl_type = "Static&nbsp;(File)";
                break;
            case "3":
                pl_type = "Static&nbsp;(iTunes)";
                break;
        }
        var row = document.createElement("tr");
        row.onclick=selectPlaylist;
        if(row.captureEvents) row.captureEvents(Event.CLICK);
        var td1 = document.createElement("td");
        var td2 = document.createElement("td");
        var td3 = document.createElement("td");
        td1.innerHTML=pl_id;
        td2.innerHTML=pl_name + "\n";
        td3.innerHTML=pl_type + "\n";
        row.appendChild(td1);
        row.appendChild(td2);
        row.appendChild(td3);
        pl_table.appendChild(row);
    }
}


function processReqChange() {
    if(req.readyState == 4) {
        if(req.status == 200) {
            processPlaylists();
        }
    }
}

function init() {
    loadXMLDoc("/databases/1/containers?output=xml&meta=dmap.itemid,dmap.itemname,org.mt-daapd.playlist-type,org.mt-daapd.smart-playlist-spec","playlists");        
}


function loadXMLDoc(url) {
    // branch for native XMLHttpRequest object
    if (window.XMLHttpRequest) {
        req = new XMLHttpRequest();
        req.onreadystatechange = processReqChange;
        req.open("GET", url, true);
        req.send(null);
    // branch for IE/Windows ActiveX version
    } else if (window.ActiveXObject) {
        req = new ActiveXObject("Microsoft.XMLHTTP");
        if (req) {
            req.onreadystatechange = processReqChange;
            req.open("GET", url, true);
            req.send();
        }
    }
}
