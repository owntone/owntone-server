#ifndef __FFMPEG_COMPAT_H__
#define __FFMPEG_COMPAT_H__

#ifdef HAVE_LIBAVUTIL_CHANNEL_LAYOUT_H
# include <libavutil/channel_layout.h>
#endif

#ifdef HAVE_LIBAVUTIL_MATHEMATICS_H
# include <libavutil/mathematics.h>
#endif

#ifndef HAVE_FFMPEG
# define avcodec_find_best_pix_fmt_of_list(a, b, c, d) avcodec_find_best_pix_fmt2((enum AVPixelFormat *)(a), (b), (c), (d))
#endif

#if !HAVE_DECL_AV_FRAME_ALLOC
# define av_frame_alloc() avcodec_alloc_frame()
# define av_frame_free(x) avcodec_free_frame((x))
#endif

#if !HAVE_DECL_AV_FRAME_GET_BEST_EFFORT_TIMESTAMP
# define av_frame_get_best_effort_timestamp(x) (x)->pts
#endif

#if !HAVE_DECL_AV_IMAGE_GET_BUFFER_SIZE
# define av_image_get_buffer_size(a, b, c, d) avpicture_get_size((a), (b), (c))
#endif

#if !HAVE_DECL_AV_PACKET_UNREF
# define av_packet_unref(a) av_free_packet((a))
#endif

#if !HAVE_DECL_AV_PACKET_RESCALE_TS
__attribute__((unused)) static void
av_packet_rescale_ts(AVPacket *pkt, AVRational src_tb, AVRational dst_tb)
{
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts = av_rescale_q(pkt->pts, src_tb, dst_tb);
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts = av_rescale_q(pkt->dts, src_tb, dst_tb);
    if (pkt->duration > 0)
        pkt->duration = av_rescale_q(pkt->duration, src_tb, dst_tb);
    if (pkt->convergence_duration > 0)
        pkt->convergence_duration = av_rescale_q(pkt->convergence_duration, src_tb, dst_tb);
}
#endif

#if !HAVE_DECL_AVFORMAT_ALLOC_OUTPUT_CONTEXT2
# include <libavutil/opt.h>

__attribute__((unused)) static int
avformat_alloc_output_context2(AVFormatContext **avctx, AVOutputFormat *oformat, const char *format, const char *filename)
{
    AVFormatContext *s = avformat_alloc_context();
    int ret = 0;

    *avctx = NULL;
    if (!s)
        goto nomem;

    if (!oformat) {
        if (format) {
            oformat = av_guess_format(format, NULL, NULL);
            if (!oformat) {
                av_log(s, AV_LOG_ERROR, "Requested output format '%s' is not a suitable output format\n", format);
                ret = AVERROR(EINVAL);
                goto error;
            }
        } else {
            oformat = av_guess_format(NULL, filename, NULL);
            if (!oformat) {
                ret = AVERROR(EINVAL);
                av_log(s, AV_LOG_ERROR, "Unable to find a suitable output format for '%s'\n",
                       filename);
                goto error;
            }
        }
    }

    s->oformat = oformat;
    if (s->oformat->priv_data_size > 0) {
        s->priv_data = av_mallocz(s->oformat->priv_data_size);
        if (!s->priv_data)
            goto nomem;
        if (s->oformat->priv_class) {
            *(const AVClass**)s->priv_data= s->oformat->priv_class;
            av_opt_set_defaults(s->priv_data);
        }
    } else
        s->priv_data = NULL;

    if (filename)
        snprintf(s->filename, sizeof(s->filename), "%s", filename);
    *avctx = s;
    return 0;
nomem:
    av_log(s, AV_LOG_ERROR, "Out of memory\n");
    ret = AVERROR(ENOMEM);
error:
    avformat_free_context(s);
    return ret;
}
#endif

#endif /* !__FFMPEG_COMPAT_H__ */
