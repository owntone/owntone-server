/*
 * $Id$
 * Build daap structs for replies
 *
 * Copyright (C) 2003 Ron Pedde (ron@pedde.com)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db-memory.h"
#include "daap-proto.h"
#include "daap.h"
#include "err.h"

typedef struct tag_daap_items {
    int type;
    char *tag;
    char *description;
} DAAP_ITEMS;

DAAP_ITEMS taglist[] = {
	{ 0x05, "miid", "dmap.itemid" },
	{ 0x09, "minm", "dmap.itemname" },
	{ 0x01, "mikd", "dmap.itemkind" },
	{ 0x07, "mper", "dmap.persistentid" },
	{ 0x0C, "mcon", "dmap.container" },
	{ 0x05, "mcti", "dmap.containeritemid" },
	{ 0x05, "mpco", "dmap.parentcontainerid" },
	{ 0x05, "mstt", "dmap.status" },
	{ 0x09, "msts", "dmap.statusstring" },
	{ 0x05, "mimc", "dmap.itemcount" },
	{ 0x05, "mctc", "dmap.containercount" },
	{ 0x05, "mrco", "dmap.returnedcount" },
	{ 0x05, "mtco", "dmap.specifiedtotalcount" },
	{ 0x0C, "mlcl", "dmap.listing" },
	{ 0x0C, "mlit", "dmap.listingitem" },
	{ 0x0C, "mbcl", "dmap.bag" },
	{ 0x0C, "mdcl", "dmap.dictionary" },
	{ 0x0C, "msrv", "dmap.serverinforesponse" },
	{ 0x01, "msau", "dmap.authenticationmethod" },
	{ 0x01, "mslr", "dmap.loginrequired" },
	{ 0x0B, "mpro", "dmap.protocolversion" },
	{ 0x01, "msal", "dmap.supportsautologout" },
	{ 0x01, "msup", "dmap.supportsupdate" },
	{ 0x01, "mspi", "dmap.supportspersistentids" },
	{ 0x01, "msex", "dmap.supportsextensions" },
	{ 0x01, "msbr", "dmap.supportsbrowse" },
	{ 0x01, "msqy", "dmap.supportsquery" },
	{ 0x01, "msix", "dmap.supportsindex" },
	{ 0x01, "msrs", "dmap.supportsresolve" },
	{ 0x05, "mstm", "dmap.timeoutinterval" },
	{ 0x05, "msdc", "dmap.databasescount" },
	{ 0x0C, "mlog", "dmap.loginresponse" },
	{ 0x05, "mlid", "dmap.sessionid" },
	{ 0x0C, "mupd", "dmap.updateresponse" },
	{ 0x05, "musr", "dmap.serverrevision" },
	{ 0x01, "muty", "dmap.updatetype" },
	{ 0x0C, "mudl", "dmap.deletedidlisting" },
	{ 0x0C, "mccr", "dmap.contentcodesresponse" },
	{ 0x05, "mcnm", "dmap.contentcodesnumber" },
	{ 0x09, "mcna", "dmap.contentcodesname" },
	{ 0x03, "mcty", "dmap.contentcodestype" },
	{ 0x0B, "apro", "daap.protocolversion" },
	{ 0x0C, "avdb", "daap.serverdatabases" },
	{ 0x0C, "abro", "daap.databasebrowse" },
	{ 0x0C, "abal", "daap.browsealbumlisting" },
	{ 0x0C, "abar", "daap.browseartistlisting" },
	{ 0x0C, "abcp", "daap.browsecomposerlisting" },
	{ 0x0C, "abgn", "daap.browsegenrelisting" },
	{ 0x0C, "adbs", "daap.databasesongs" },
	{ 0x09, "asal", "daap.songalbum" },
	{ 0x09, "asar", "daap.songartist" },
	{ 0x03, "asbt", "daap.songbeatsperminute" },
	{ 0x03, "asbr", "daap.songbitrate" },
	{ 0x09, "ascm", "daap.songcomment" },
	{ 0x01, "asco", "daap.songcompilation" },
	{ 0x09, "ascp", "daap.songcomposer" },
	{ 0x0A, "asda", "daap.songdateadded" },
	{ 0x0A, "asdm", "daap.songdatemodified" },
	{ 0x03, "asdc", "daap.songdisccount" },
	{ 0x03, "asdn", "daap.songdiscnumber" },
	{ 0x01, "asdb", "daap.songdisabled" },
	{ 0x09, "aseq", "daap.songeqpreset" },
	{ 0x09, "asfm", "daap.songformat" },
	{ 0x09, "asgn", "daap.songgenre" },
	{ 0x09, "asdt", "daap.songdescription" },
	{ 0x02, "asrv", "daap.songrelativevolume" },
	{ 0x05, "assr", "daap.songsamplerate" },
	{ 0x05, "assz", "daap.songsize" },
	{ 0x05, "asst", "daap.songstarttime" },
	{ 0x05, "assp", "daap.songstoptime" },
	{ 0x05, "astm", "daap.songtime" },
	{ 0x03, "astc", "daap.songtrackcount" },
	{ 0x03, "astn", "daap.songtracknumber" },
	{ 0x01, "asur", "daap.songuserrating" },
	{ 0x03, "asyr", "daap.songyear" },
	{ 0x01, "asdk", "daap.songdatakind" },
	{ 0x09, "asul", "daap.songdataurl" },
	{ 0x0C, "aply", "daap.databaseplaylists" },
	{ 0x01, "abpl", "daap.baseplaylist" },
	{ 0x0C, "apso", "daap.playlistsongs" },
	{ 0x0C, "arsv", "daap.resolve" },
	{ 0x0C, "arif", "daap.resolveinfo" },
	{ 0x05, "aeNV", "com.apple.itunes.norm-volume" },
	{ 0x01, "aeSP", "com.apple.itunes.smart-playlist" },
	{ 0x00, NULL,   NULL }
};

/* Forwards */
DAAP_BLOCK *daap_response_songlist(void);
DAAP_BLOCK *daap_response_playlists(void);
DAAP_BLOCK *daap_response_dbinfo(void);
DAAP_BLOCK *daap_response_playlist_items(int playlist);

int daap_add_mdcl(DAAP_BLOCK *root, char *tag, char *name, short int number) {
    DAAP_BLOCK *mdcl;
    int g=1;

    mdcl=daap_add_empty(root,"mdcl");
    if(mdcl) {
	g=(int)daap_add_string(mdcl,"mcnm",tag);
	g = g && daap_add_string(mdcl,"mcna",name);
	g = g && daap_add_short(mdcl,"mcty",number);
    }

    return (mdcl ? g : 0);
}

/*
 * daap_response_content_codes
 *
 * handle the daap block for the /content-codes URI
 *
 * This might more easily be done by just emitting a binary
 * of the content-codes from iTunes, since this really
 * isn't dynamic
 */

DAAP_BLOCK *daap_response_content_codes(void) {
    DAAP_BLOCK *root;
    DAAP_ITEMS *current=taglist;
    int g=1;

    root=daap_add_empty(NULL,"mccr");
    if(root) {
	g = (int)daap_add_int(root,"mstt",200);

	while(current->type) {
	    g = g && daap_add_mdcl(root,current->tag,current->description,
				   current->type);
	    current++;
	}
    }

    if(!g) {
	daap_free(root);
	return NULL;
    }

    return root;
}


/*
 * daap_response_login
 *
 * handle the daap block for the /login URI
 */

DAAP_BLOCK *daap_response_login(void) {
    DAAP_BLOCK *root;
    int g=1;
    

    root=daap_add_empty(NULL,"mlog");
    if(root) {
	g = (int)daap_add_int(root,"mstt",200);
	g = g && daap_add_int(root,"mlid",7);  /* static id! */
    }

    if(!g) {
	daap_free(root);
	return NULL;
    }

    return root;
}

/*
 * daap_response_songlist
 *
 * handle the daap block for the /databases/x/items URI
 */

DAAP_BLOCK *daap_response_songlist(void) {
    DAAP_BLOCK *root;
    int g=1;
    DAAP_BLOCK *mlcl;
    DAAP_BLOCK *mlit;
    ENUMHANDLE henum;
    MP3FILE *current;

    henum=db_enum_begin();
    if(!henum)
	return NULL;

    root=daap_add_empty(NULL,"adbs");
    if(root) {
	g = (int)daap_add_int(root,"mstt",200);
	g = g && daap_add_char(root,"muty",0);
	g = g && daap_add_int(root,"mtco",db_get_song_count());
	g = g && daap_add_int(root,"mrco",db_get_song_count());

	mlcl=daap_add_empty(root,"mlcl");

	if(mlcl) {
	    while(current=db_enum(&henum)) {
	        DPRINTF(ERR_DEBUG,"Got entry for %s\n",current->fname);
		mlit=daap_add_empty(mlcl,"mlit");
		if(mlit) {
		    g = g && daap_add_char(mlit,"mikd",2); /* audio */
		    g = g && daap_add_string(mlit,"asal",current->album);
		    g = g && daap_add_string(mlit,"asar",current->artist);
		    g = g && daap_add_short(mlit,"asbt",0); /* bpm */
		    g = g && daap_add_short(mlit,"asbr",128); /* bitrate!! */
		    g = g && daap_add_string(mlit,"ascm","ron was here"); /* comment */
		    g = g && daap_add_char(mlit,"asco",0x0); /* compilation */
		    g = g && daap_add_string(mlit,"ascp",""); /* composer */
		    g = g && daap_add_int(mlit,"asda",0); /* date added */
		    g = g && daap_add_int(mlit,"asdm",0); /* date modified */
		    g = g && daap_add_short(mlit,"asdc",0); /* # of discs */
		    g = g && daap_add_short(mlit,"asdn",0); /* disc number */
		    g = g && daap_add_char(mlit,"asdk",0); /* song datakind? */
		    g = g && daap_add_string(mlit,"asfm","mp3"); /* song format */
		    // aseq - null string!
		    g = g && daap_add_string(mlit,"asgn",current->genre); /* genre */
		    g = g && daap_add_int(mlit,"miid",current->id); /* id */
		    g = g && daap_add_string(mlit,"asdt","MPEG audio file"); /* descr */
		    g = g && daap_add_string(mlit,"minm",current->fname); /* descr */
		    // mper (long)
		    g = g && daap_add_char(mlit,"asdb",0); /* disabled */
		    g = g && daap_add_char(mlit,"asrv",0); /* rel vol */
		    g = g && daap_add_int(mlit,"assr",44100); /* sample rate */
		    g = g && daap_add_int(mlit,"assz",1024); /* FIXME: Song size! */
		    g = g && daap_add_int(mlit,"asst",0); /* song start time? */
		    g = g && daap_add_int(mlit,"assp",0); /* songstoptime */
		    g = g && daap_add_int(mlit,"astm",3600); /* song time */
		    g = g && daap_add_short(mlit,"astc",0); /* track count */
		    g = g && daap_add_short(mlit,"astn",0); /* track number */
		    g = g && daap_add_char(mlit,"asur",3); /* rating */
		    g = g && daap_add_short(mlit,"asyr",0);
		} else g=0;
	    }
	} else g=0;
    }

    db_enum_end();

    if(!g) {
	daap_free(root);
	return NULL;
    }

    return root;
}


/*
 * daap_response_update
 *
 * handle the daap block for the /update URI
 */

DAAP_BLOCK *daap_response_update(int clientver) {
    DAAP_BLOCK *root;
    int g=1;

    while(clientver == db_version()) {
	sleep(30);
    }

    root=daap_add_empty(NULL,"mupd");
    if(root) {
	g = (int)daap_add_int(root,"mstt",200);
	/* theoretically, this would go up if the db changes? */
	g = g && daap_add_int(root,"musr",db_version());
    }

    if(!g) {
	daap_free(root);
	return NULL;
    }

    return root;
}

/*
 * daap_response_databases
 *
 * handle the daap block for the /databases URI
 */

DAAP_BLOCK *daap_response_databases(char *path) {
    char *uri;
    int db_index;
    int playlist_index;
    char *first, *last;

    if(strcmp(path,"/databases")==0) {
	return daap_response_dbinfo();
    }


    uri = strdup(path);
    first=(char*)&uri[11];
    last=first;
    while((*last) && (*last != '/')) {
	last++;
    }
    
    if(*last != '/') {
	return NULL;
    }
    
    *last='\0';
    db_index=atoi(first);
    
    /* now we have the db id.  Next, we have to figure out
     * if it's a container request or a 
     * items request
     *
     * note we generally don't care about the DB index, since we only
     * support 1 db.
     */
    
    /* the /databases/ uri will either be
     *
     * /databases, which returns an AVDB,
     * /databases/id/items, which returns items in a db
     * /databases/id/containers, which returns a container
     * /databases/id/containers/id/items, which returns playlist elements
     * /databases/id/items/id.mp3, to spool an mp3
     */

    last++;

    if(strncasecmp(last,"items/",6)==0) {
	/* streaming */
	free(uri);
	return NULL;
    }

    if(strncasecmp(last,"items",5)==0) {
	/* songlist */
	free(uri);
	return daap_response_songlist();
    }

    if(strncasecmp(last,"containers/",11)==0) {
	/* playlist elements */
	first=last + 11;
	last=first;
	while((*last) && (*last != '/')) {
	    last++;
	}
	
	if(*last != '/') {
	    return NULL;
	}
	
	*last='\0';
	playlist_index=atoi(first);

	free(uri);

	return daap_response_playlist_items(playlist_index);
    }

    if(strncasecmp(last,"containers",10)==0) {
	/* list of playlists */
	free(uri);
	return daap_response_playlists();
	return NULL;
    }

    free(uri);
    return NULL;

}

/*
 * daap_response_playlists
 *
 * handle the daap block for the /databases/containers URI
 */
DAAP_BLOCK *daap_response_playlists(void) {
    DAAP_BLOCK *root;
    DAAP_BLOCK *mlcl;
    DAAP_BLOCK *mlit;
    int g=1;
    
    root=daap_add_empty(NULL,"aply");
    if(root) {
	g = (int)daap_add_int(root,"mstt",200);
	g = g && daap_add_char(root,"muty",0); 
	g = g && daap_add_int(root,"mtco",1);
	g = g && daap_add_int(root,"mrco",1);
	mlcl=daap_add_empty(root,"mlcl");
	if(mlcl) {
	    mlit=daap_add_empty(mlcl,"mlit");
	    if(mlit) {
		g = g && daap_add_int(mlit,"miid",0x1);
		g = g && daap_add_long(mlit,"mper",0,2);
		g = g && daap_add_string(mlit,"minm","mt-daapd");
		g = g && daap_add_int(mlit,"mimc",db_get_song_count());
	    }
	}
    }

    g = g && mlcl && mlit;

    if(!g) {
	DPRINTF(ERR_INFO,"Memory problem.  Bailing\n");
	daap_free(root);
	return NULL;
    }

    return root;
}

/*
 * daap_response_dbinfo
 *
 * handle the daap block for the /databases URI
 */

DAAP_BLOCK *daap_response_dbinfo(void) {
    DAAP_BLOCK *root;
    DAAP_BLOCK *mlcl;
    DAAP_BLOCK *mlit;
    int g=1;
    
    root=daap_add_empty(NULL,"avdb");
    if(root) {
	g = (int)daap_add_int(root,"mstt",200);
	g = g && daap_add_char(root,"muty",0); 
	g = g && daap_add_int(root,"mtco",1);
	g = g && daap_add_int(root,"mrco",1);
	mlcl=daap_add_empty(root,"mlcl");
	if(mlcl) {
	    mlit=daap_add_empty(mlcl,"mlit");
	    if(mlit) {
		g = g && daap_add_int(mlit,"miid",0x20);
		g = g && daap_add_long(mlit,"mper",0,1);
		g = g && daap_add_string(mlit,"minm","mt-daapd");
		g = g && daap_add_int(mlit,"mimc",db_get_song_count()); /* songs */
		g = g && daap_add_int(mlit,"mctc",0x1); /* playlists */
	    }
	}
    }

    g = g && mlcl && mlit;

    if(!g) {
	DPRINTF(ERR_INFO,"Memory problem.  Bailing\n");
	daap_free(root);
	return NULL;
    }

    return root;
}

/*
 * daap_response_server_info
 *
 * handle the daap block for the /server-info URI
 */
DAAP_BLOCK *daap_response_server_info(void) {
    DAAP_BLOCK *root;
    int g=1;

    root=daap_add_empty(NULL,"msrv");

    if(root) {
	g = (int)daap_add_int(root,"mstt",200); /* result */
	g = g && daap_add_int(root,"mpro",2 << 16); /* dmap proto ? */
	g = g && daap_add_int(root,"apro",2 << 16); /* daap protocol */
	g = g && daap_add_string(root,"minm","mt-daapd"); /* server name */
	g = g && daap_add_char(root,"mslr",0); /* logon required */
	g = g && daap_add_int(root,"mstm",1800); /* timeout  - iTunes=1800 */
	g = g && daap_add_char(root,"msal",0); /* autologout */
	g = g && daap_add_char(root,"msup",1); /* update */
	g = g && daap_add_char(root,"mspi",0); /* persistant ids */
	g = g && daap_add_char(root,"msex",0); /* extensions */
	g = g && daap_add_char(root,"msbr",0); /* browsing */
	g = g && daap_add_char(root,"msqy",0); /* queries */
	g = g && daap_add_char(root,"msix",0); /* indexing? */
	g = g && daap_add_char(root,"msrs",0); /* resolve?  req. persist id */
        g = g && daap_add_int(root,"msdc",1); /* database count */
    }

    if(!g) {
	daap_free(root);
	return NULL;
    }

    return root;
}


/* 
 * daap_response_playlist_items
 *
 * given a playlist number, return the items on the playlist
 */
DAAP_BLOCK *daap_response_playlist_items(int playlist) {
    DAAP_BLOCK *root;
    DAAP_BLOCK *mlcl;
    DAAP_BLOCK *mlit;
    ENUMHANDLE henum;
    MP3FILE *current;
    int g=1;
    
    henum=db_enum_begin();
    if(!henum)
	return NULL;

    root=daap_add_empty(NULL,"apso");
    if(root) {
	g = (int)daap_add_int(root,"mstt",200);
	g = g && daap_add_char(root,"muty",0);
	g = g && daap_add_int(root,"mtco",0);
	g = g && daap_add_int(root,"mrco",0);

	mlcl=daap_add_empty(root,"mlcl");

	if(mlcl) {
	    while(current=db_enum(&henum)) {
		mlit=daap_add_empty(mlcl,"mlit");
		if(mlit) {
		    g = g && daap_add_char(mlit,"mikd",2);
		    g = g && daap_add_int(mlit,"miid",current->id);
		    g = g && daap_add_int(mlit,"mcti",0x1); /* built-in container */
		} else g=0;
	    }
	} else g=0;
    }

    db_enum_end();

    if(!g) {
	daap_free(root);
	return NULL;
    }

    return root;
}
