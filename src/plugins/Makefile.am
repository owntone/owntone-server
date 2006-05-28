# $Id: $
#
rspdir = ${pkgdatadir}/plugins
ssc_ffmpegdir = ${pkgdatadir}/plugins
ssc_scriptdir = ${pkgdatadir}/plugins

rsp_LTLIBRARIES=rsp.la
rsp_la_LDFLAGS=-module -avoid-version
rsp_la_SOURCES = compat.c rsp.c xml-rpc.c

if COND_FFMPEG
ssc_ffmpeg_LTLIBRARIES=ssc-ffmpeg.la
ssc_ffmpeg_la_LDFLAGS=-module -avoid-version
ssc_ffmpeg_la_SOURCES=ssc-ffmpeg.c
endif

ssc_script_LTLIBRARIES=ssc-script.la
ssc_script_la_LDFLAGS=-module -avoid-version
ssc_script_la_SOURCES=ssc-script.c

EXTRA_DIST = compat.h rsp.h mtd-plugins.h xml-rpc.h ssc-ffmpeg.c ssc-script.c

AM_CFLAGS = -I..

