/*
 * Copyright (c) 2013 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat/libavcodec demuxing and muxing API example.
 *
 * Remux streams from one container format to another.
 * @example remuxing.c
 */

#include <string>
#include <memory>
#include <functional>

extern "C" {
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
}

static const std::string av_make_error_string(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return (std::string)errbuf;
}

static const std::string av_ts_make_time_string(int64_t ts, AVRational *tb)
{
    char buf[AV_TS_MAX_STRING_SIZE];
    if (ts == AV_NOPTS_VALUE) snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
    else                      snprintf(buf, AV_TS_MAX_STRING_SIZE, "%.6g", av_q2d(*tb) * ts);
    return (std::string)buf;
}

#undef av_err2str
#define av_err2str(errnum) av_make_error_string(errnum).c_str()

#undef av_ts2str
#define av_ts2str(errnum) av_make_error_string(errnum).c_str()

#undef av_ts2timestr
#define av_ts2timestr(ts, tb) av_ts_make_time_string(ts, tb).c_str()

typedef std::unique_ptr<AVFormatContext, std::function<void (AVFormatContext *)>> AVFmtCtx;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: %s input output\n"
                       "API example program to remux a media file with libavformat and libavcodec.\n"
                       "The output format is guessed according to the file extension.\n"
                       "\n", argv[0]);
        return 1;
    }

    std::string in_filename  = argv[1];
    std::string out_filename = argv[2];

    av_register_all();

    AVFmtCtx input_fmt_ctx  = AVFmtCtx(nullptr, [](AVFormatContext *context) { avformat_close_input(&context); });
    AVFmtCtx output_fmt_ctx = AVFmtCtx(nullptr, [](AVFormatContext *context) { avformat_free_context(context); });

    int ret = 0;
    do {

        /*
         * Input format context
         */
        input_fmt_ctx.reset(avformat_alloc_context());
        auto ifmt_ctx = input_fmt_ctx.get();
        if ((ret = avformat_open_input(&ifmt_ctx, in_filename.c_str(), 0, 0)) < 0) {
            fprintf(stderr, "Could not open input file '%s'", in_filename.c_str());
            break;
        }
        if ((ret = avformat_find_stream_info(input_fmt_ctx.get(), 0)) < 0) {
            fprintf(stderr, "Failed to retrieve input stream information");
            break;
        }
        av_dump_format(input_fmt_ctx.get(), 0, in_filename.c_str(), 0);

        /*
         * Output format context
         */
        AVFormatContext *ctx = NULL;
        avformat_alloc_output_context2(&ctx, NULL, NULL, out_filename.c_str());
        if (!ctx) {
            fprintf(stderr, "Could not create output context\n");
            ret = AVERROR_UNKNOWN;
            break;
        }
        output_fmt_ctx.reset(ctx);

        for (int i = 0; i < input_fmt_ctx->nb_streams; i++) {
            AVStream *in_stream = input_fmt_ctx->streams[i];
            AVStream *out_stream = avformat_new_stream(output_fmt_ctx.get(), in_stream->codec->codec);
            if (!out_stream) {
                fprintf(stderr, "Failed allocating output stream\n");
                ret = AVERROR_UNKNOWN;
                break;
            }

            ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
            if (ret < 0) {
                fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                break;
            }
            out_stream->codec->codec_tag = 0;
            if (output_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
        av_dump_format(output_fmt_ctx.get(), 0, out_filename.c_str(), 1);

        if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&output_fmt_ctx->pb, out_filename.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                fprintf(stderr, "Could not open output file '%s'", out_filename.c_str());
                break;
            }
        }

        /* Write the stream header, if any. */
        AVDictionary *movflags = nullptr;
        av_dict_set(&movflags, "movflags", "empty_moov+default_base_moof+frag_keyframe", 0);
        if ((ret = avformat_write_header(output_fmt_ctx.get(), &movflags)) < 0) {
            fprintf(stderr, "Error occurred when opening output file: %s\n", av_err2str(ret));
            break;
        }
        av_dict_free(&movflags);

        AVPacket pkt;
        while (1) {

            AVStream *in_stream = NULL, *out_stream = NULL;

            if ((ret = av_read_frame(input_fmt_ctx.get(), &pkt)) < 0) break;
            {
                in_stream  = input_fmt_ctx->streams[pkt.stream_index];
                out_stream = output_fmt_ctx->streams[pkt.stream_index];

                log_packet(input_fmt_ctx.get(), &pkt, "in");
                {
                    /* copy packet */
                    AVRounding rounding = static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
                    pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, rounding);
                    pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, rounding);
                    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
                    pkt.pos = -1;
                }
                log_packet(output_fmt_ctx.get(), &pkt, "out");

                if ((ret = av_interleaved_write_frame(output_fmt_ctx.get(), &pkt)) < 0) {
                    fprintf(stderr, "Error muxing packet\n");
                    break;
                }
            }

            av_packet_unref(&pkt);
        }

        av_write_trailer(output_fmt_ctx.get());

    } while(false);

    /* close output */
    if (output_fmt_ctx && !(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_fmt_ctx->pb);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}