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
	bitrate		INTEGER DEFAULT NULL,
	samplerate	INTEGER DEFAULT NULL,
	song_length	INTEGER DEFAULT NULL,
	file_size	INTEGER DEFAULT NULL,
	year		INTEGER DEFAULT NULL,
	track		INTEGER DEFAULT NULL,
	total_tracks	INTEGER DEFAULT NULL,
	disc		INTEGER DEFAULT NULL,
	total_discs	INTEGER DEFAULT NULL,
	time_added	INTEGER DEFAULT NULL,
	time_modified	INTEGER DEFAULT NULL,
	time_played	INTEGER	DEFAULT NULL,
	db_timestamp	INTEGER DEFAULT NULL,
	bpm		INTEGER DEFAULT NULL,
	compilation	INTEGER DEFAULT NULL,
	play_count	INTEGER DEFAULT NULL,
	rating		INTEGER DEFAULT NULL
);	

CREATE TABLE config (
	term		VARCHAR(255)	NOT NULL,
	value		VARCHAR(1024)	NOT NULL
);

CREATE TABLE playlists (
       id    	       INTEGER PRIMARY KEY NOT NULL,
       name	       VARCHAR(255) NOT NULL,
       smart	       INTEGER NOT NULL,
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

INSERT INTO config (term, value) VALUES ('version','1');
INSERT INTO users(id,name,password) VALUES (1,'admin','mt-daapd');
INSERT INTO playlists(id,name,smart,query) VALUES (1,'Library',1,'1');

