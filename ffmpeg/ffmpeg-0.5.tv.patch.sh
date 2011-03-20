#!/bin/sh
#
# Patch for ffmpeg so forked-daapd can extract iTunes TV metadata from mp4 video files
# Ace Jones <ace.jones1@yahoo.com>
# 
# Usage: chmod +x this file, and run it from the directory above where you want ffmpeg
#
svn checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
patch -p0 < $0
exit
Index: ffmpeg/libavformat/mov.c
===================================================================
--- ffmpeg/libavformat/mov.c	(revision 20790)
+++ ffmpeg/libavformat/mov.c	(working copy)
@@ -80,19 +80,45 @@
 
 static const MOVParseTableEntry mov_default_parse_table[];
 
-static int mov_metadata_trkn(MOVContext *c, ByteIOContext *pb, unsigned len)
+static int mov_metadata_trkn(MOVContext *c, ByteIOContext *pb, unsigned len, const char *key)
 {
     char buf[16];
 
     get_be16(pb); // unknown
     snprintf(buf, sizeof(buf), "%d", get_be16(pb));
-    av_metadata_set(&c->fc->metadata, "track", buf);
+    av_metadata_set(&c->fc->metadata, key, buf);
 
     get_be16(pb); // total tracks
 
     return 0;
 }
 
+static int mov_metadata_int8(MOVContext *c, ByteIOContext *pb, unsigned len, const char *key)
+{
+  char buf[16];
+  
+  /* bypass padding bytes */
+  get_byte(pb);
+  get_byte(pb);
+  get_byte(pb);
+  
+  snprintf(buf, sizeof(buf-1), "%hu", get_byte(pb));
+  buf[sizeof(buf)-1] = 0;
+  av_metadata_set(&c->fc->metadata, key, buf);
+
+  return 0;
+}
+
+static int mov_metadata_stik(MOVContext *c, ByteIOContext *pb, unsigned len, const char *key)
+{
+  char buf[16];
+  
+  snprintf(buf, sizeof(buf-1), "%hu", get_byte(pb));
+  buf[sizeof(buf)-1] = 0;
+  av_metadata_set(&c->fc->metadata, key, buf);
+
+  return 0;
+}
 static int mov_read_udta_string(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
 {
 #ifdef MOV_EXPORT_ALL_METADATA
@@ -101,7 +127,7 @@
     char str[1024], key2[16], language[4] = {0};
     const char *key = NULL;
     uint16_t str_size;
-    int (*parse)(MOVContext*, ByteIOContext*, unsigned) = NULL;
+    int (*parse)(MOVContext*, ByteIOContext*, unsigned, const char*) = NULL;
 
     switch (atom.type) {
     case MKTAG(0xa9,'n','a','m'): key = "title";     break;
@@ -122,6 +148,12 @@
     case MKTAG( 't','v','s','h'): key = "show";      break;
     case MKTAG( 't','v','e','n'): key = "episode_id";break;
     case MKTAG( 't','v','n','n'): key = "network";   break;
+    case MKTAG( 't','v','e','s'): key = "episode_sort";
+        parse = mov_metadata_int8; break;
+    case MKTAG( 't','v','s','n'): key = "season_number";
+        parse = mov_metadata_int8; break;
+    case MKTAG( 's','t','i','k'): key = "stik";
+        parse = mov_metadata_stik; break;
     case MKTAG( 't','r','k','n'): key = "track";
         parse = mov_metadata_trkn; break;
     }
@@ -157,10 +189,11 @@
     str_size = FFMIN3(sizeof(str)-1, str_size, atom.size);
 
     if (parse)
-        parse(c, pb, str_size);
+        parse(c, pb, str_size, key);
     else {
         get_buffer(pb, str, str_size);
         str[str_size] = 0;
+
         av_metadata_set(&c->fc->metadata, key, str);
         if (*language && strcmp(language, "und")) {
             snprintf(key2, sizeof(key2), "%s-%s", key, language);
