CREATE TABLE songs (
	id		INTEGER PRIMARY KEY NOT NULL,
	path		VARCHAR(4096) NOT NULL,
	fname		VARCHAR(255) NOT NULL,
        title		VARCHAR(1024) DEFAULT NULL,
	artist 		VARCHAR(1024) DEFAULT NULL,
	album		VARCHAR(1024) DEFAULT NULL,
	genre		VARCHAR(255) DEFAULT NULL,
	comment 	VARCHAR(4096) DEFAULT NULL,
	type		VARCHAR(255) DEFAULT NULL,
	composer	VARCHAR(1024) DEFAULT NULL,
	orchestra	VARCHAR(1024) DEFAULT NULL,
	conductor	VARCHAR(1024) DEFAULT NULL,
	grouping	VARCHAR(1024) DEFAULT NULL,
	url		VARCHAR(1024) DEFAULT NULL,
	bitrate		INTEGER DEFAULT 0,
	samplerate	INTEGER DEFAULT 0,
	song_length	INTEGER DEFAULT 0,
	file_size	INTEGER DEFAULT 0,
	year		INTEGER DEFAULT 0,
	track		INTEGER DEFAULT 0,
	total_tracks	INTEGER DEFAULT 0,
	disc		INTEGER DEFAULT 0,
	total_discs	INTEGER DEFAULT 0,
	bpm		INTEGER DEFAULT 0,
	compilation	INTEGER DEFAULT 0,
	rating		INTEGER DEFAULT 0,
	play_count	INTEGER DEFAULT 0,
	data_kind	INTEGER DEFAULT 0,
	item_kind	INTEGER DEFAULT 0,
	description	INTEGER DEFAULT 0,
	time_added	INTEGER DEFAULT 0,
	time_modified	INTEGER DEFAULT 0,
	time_played	INTEGER	DEFAULT 0,
	db_timestamp	INTEGER DEFAULT 0,
	disabled        INTEGER DEFAULT 0,
	updated		INTEGER DEFAULT 0,
	force_update	INTEGER DEFAULT 0
);	

CREATE TABLE config (
	term		VARCHAR(255)	NOT NULL,
	value		VARCHAR(1024)	NOT NULL
);

CREATE TABLE playlists (
       id    	       INTEGER PRIMARY KEY NOT NULL,
       name	       VARCHAR(255) NOT NULL,
       smart	       INTEGER NOT NULL,
       items	       INTEGER NOT NULL,
       query	       VARCHAR(1024)
);

CREATE TABLE playlistitems (
       id              INTEGER NOT NULL,
       songid	       INTEGER NOT NULL
);

CREATE TABLE users (
       id              INTEGER PRIMARY KEY NOT NULL,
       library	       INTEGER NOT NULL DEFAULT 1,
       name	       VARCHAR(255) NOT NULL,
       password	       VARCHAR(255) NOT NULL
);
       
;CREATE INDEX idx_path on songs(path);

INSERT INTO config VALUES ('version','1');
INSERT INTO users VALUES (1,1,'admin','mt-daapd');
INSERT INTO playlists VALUES (1,'Library',1,0,'1');

