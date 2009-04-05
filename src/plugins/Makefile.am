
rspdir = ${pkglibdir}/plugins
out_daapdir = ${pkglibdir}/plugins
ssc_ffmpegdir = ${pkglibdir}/plugins
ssc_scriptdir = ${pkglibdir}/plugins

rsp_LTLIBRARIES=rsp.la
rsp_la_LDFLAGS=-module -avoid-version
rsp_la_SOURCES = rsp.c xml-rpc.c

ssc_ffmpeg_LTLIBRARIES=ssc-ffmpeg.la
ssc_ffmpeg_la_CPPFLAGS = @FFMPEG_CFLAGS@
ssc_ffmpeg_la_LIBADD = @FFMPEG_LIBS@
ssc_ffmpeg_la_LDFLAGS=-module -avoid-version
ssc_ffmpeg_la_SOURCES=ssc-ffmpeg.c

ssc_script_LTLIBRARIES=ssc-script.la
ssc_script_la_LDFLAGS=-module -avoid-version
ssc_script_la_SOURCES=ssc-script.c

out_daap_LTLIBRARIES=out-daap.la
out_daap_la_LDFLAGS=-module -avoid-version
out_daap_la_SOURCES=out-daap.c out-daap-proto.c

EXTRA_DIST = rsp.h xml-rpc.h ssc-ffmpeg.c ssc-script.c out-daap.h \
	out-daap-proto.h

AM_CFLAGS = -I..

