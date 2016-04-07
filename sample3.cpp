#include <string>

#include <mp4v2/mp4v2.h>
#include <netinet/in.h>

extern "C" {
    #include <libavformat/avformat.h>
};

/*
    FIX: H.264 in some container format (FLV, MP4, MKV etc.) need
    "h264_mp4toannexb" bitstream filter (BSF)
      *Add SPS,PPS in front of IDR frame
      *Add start code ("0,0,0,1") in front of NALU
    H.264 in some container (MPEG2TS) don't need this BSF.
*/
//'1': Use H.264 Bitstream Filter
#define USE_H264BSF 0


class MP4Reader
{
public:

    enum MP4ReadStatus
    {
        MP4_READ_OK,
        MP4_READ_EOS,
        MP4_READ_ERR
    };

    MP4Reader(const std::string &file_path)
            : time_scale(9 * MP4_MSECS_TIME_SCALE)
            , file_path(file_path)
            , handle(MP4_INVALID_FILE_HANDLE)
            , video_track_id(MP4_INVALID_TRACK_ID)
            , next_video_sample_idx(1)
            , video_sample(nullptr)
            , video_timescale(0)
            , video_sample_max_size(0)
            , video_sample_number(0)
            , video_duration(0)
    {
        handle = MP4Read(this->file_path.c_str());

        video_track_id = MP4FindTrackId(handle, 0, MP4_VIDEO_TRACK_TYPE);
        if (video_track_id != MP4_INVALID_TRACK_ID) {
            video_timescale = MP4GetTrackTimeScale(handle, video_track_id);
            video_sample_max_size = MP4GetTrackMaxSampleSize(handle, video_track_id);
            video_duration = MP4GetTrackDuration(handle, video_track_id);
            video_sample = new unsigned char[video_sample_max_size];
            video_sample_number = MP4GetTrackNumberOfSamples(handle, video_track_id);
        }
    }

    ~MP4Reader()
    {
        if (handle != MP4_INVALID_FILE_HANDLE) MP4Close(handle);
        if (video_sample) delete[] video_sample;
    }

    unsigned int GetVideoWidth() const
    {
        return MP4GetTrackVideoWidth(handle, video_track_id);
    }

    unsigned int GetVideoHeight() const
    {
        return MP4GetTrackVideoHeight(handle, video_track_id);
    }

    double GetVideoFps() const
    {
        return MP4GetTrackVideoFrameRate(handle, video_track_id);
    }

    unsigned int GetBitRate() const
    {
        return MP4GetTrackBitRate(handle, video_track_id);
    }

    MP4ReadStatus GetNextH264VideoSample(unsigned char **sample,
                                         unsigned int &sample_size,
                                         unsigned long long int &duration,
                                         bool &is_key_frame)
    {
        if (next_video_sample_idx > video_sample_number) {
            return MP4_READ_EOS;
        }

        MP4Duration mp4_duration = 0;
        sample_size = video_sample_max_size;
        if (!MP4ReadSample(handle, video_track_id, next_video_sample_idx,
                           &video_sample, &sample_size,
                           NULL,
                           &mp4_duration,
                           NULL,
                           &is_key_frame)) {
            printf("Fail to read video sample (%d)\n", next_video_sample_idx);
            return MP4_READ_ERR;
        }

        // Convert AVC1 format to AnnexB
        if (sample_size >= 4) {
            unsigned int *p = (unsigned int *) video_sample;
            *p = htonl(1);
        }

        *sample = video_sample;
        duration = (1000 * mp4_duration) / time_scale;
        next_video_sample_idx++;
        return MP4_READ_OK;
    }

private:

    const unsigned int time_scale;

    std::string file_path;
    MP4FileHandle handle;
    MP4TrackId video_track_id;
    unsigned int next_video_sample_idx;
    unsigned char *video_sample;

    unsigned int video_timescale;
    unsigned int video_sample_max_size;
    unsigned int video_sample_number;
    unsigned long long int video_duration;
};

class MP4Writer
{
public:

    MP4Writer(const std::string &file_path)
        : file_path(file_path)
        , file_duration(0)
        , format_context(nullptr)
        , video_stream_id(0)
    {
        av_register_all();
    }

    ~MP4Writer()
    {
        if (av_write_trailer(format_context) < 0) {
            printf("Fail to write trailer\n");
        }

        if (format_context && format_context->streams[video_stream_id]) {
            if (format_context->streams[video_stream_id]->codec) {
                avcodec_close(format_context->streams[video_stream_id]->codec);
            }
        }

        if (format_context && !(format_context->oformat->flags & AVFMT_NOFILE)) {
            if (avio_closep(&format_context->pb) < 0) {
                printf("Fail to close AVIO context\n");
            }
        }

        if (format_context)
            avformat_free_context(format_context);
    }

    bool AddH264VideoTrack(const unsigned int width,
                           const unsigned int height,
                           const double frame_rate,
                           const unsigned int bit_rate)
    {
        avformat_alloc_output_context2(&format_context, nullptr, "mp4", nullptr);
        if (!format_context) {
            printf("Fail to create output context\n");
            return false;
        }

        /*AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            printf("Fail to find encoder: %s\n", avcodec_get_name(AV_CODEC_ID_H264));
            return false;
        }
        AVStream *out_stream = avformat_new_stream(format_context, codec);*/

        AVStream *out_stream = avformat_new_stream(format_context, nullptr);
        if (!out_stream) {
            printf("Fail to allocate output stream\n");
            return false;
        }

        out_stream->id = video_stream_id = format_context->nb_streams - 1;
        out_stream->time_base = av_d2q(frame_rate, 100);
        out_stream->codec->time_base = av_d2q(frame_rate, 100);
        out_stream->codec->codec_id   = AV_CODEC_ID_H264;
        out_stream->codec->profile    = FF_PROFILE_H264_CONSTRAINED_BASELINE;
        out_stream->codec->level      = 40;
        out_stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        out_stream->codec->width      = width;
        out_stream->codec->height     = height;
        out_stream->codec->bit_rate   = bit_rate;
        out_stream->codec->pix_fmt    = AV_PIX_FMT_YUV420P;
        out_stream->codec->codec_tag  = 0;

        if (format_context->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

        av_dump_format(format_context, 0, file_path.c_str(), 1);

        /*
         * Open output file
         */
        {
            if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&format_context->pb, file_path.c_str(), AVIO_FLAG_WRITE) < 0) {
                    printf("Could not open output file '%s'", file_path.c_str());
                    return false;
                }
            }
        }

        /*
         * Write file header
         */
        {
            AVDictionary *movflags = NULL;
            av_dict_set(&movflags, "movflags", "empty_moov+default_base_moof+frag_keyframe", 0);
            if (avformat_write_header(format_context, &movflags) < 0) {
                printf("Error occurred when opening output file\n");
                return false;
            }
            av_dict_free(&movflags);
        }

        return true;
    }

    bool WriteH264VideoSample(unsigned char *sample,
                              unsigned int sample_size,
                              bool is_key_frame,
                              unsigned long long int duration)
    {
        AVPacket packet = { 0 };
        av_init_packet(&packet);

        packet.stream_index = video_stream_id;
        packet.data         = sample;
        packet.size         = sample_size;
        packet.pos          = -1;

        packet.dts = packet.pts = static_cast<int64_t>(file_duration);
        packet.duration = static_cast<int>(duration);
        av_packet_rescale_ts(&packet, (AVRational){1, 1000}, format_context->streams[video_stream_id]->time_base);

        if (is_key_frame) {
            packet.flags |= AV_PKT_FLAG_KEY;
        }

        if (av_interleaved_write_frame(format_context, &packet) < 0) {
            printf("Fail to write frame\n");
            return false;
        }

        file_duration += duration;

        return true;
    }

private:

    std::string file_path;
    unsigned long long int file_duration;
    AVFormatContext *format_context;
    unsigned int video_stream_id;
};

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: %s input output\n", argv[0]);
        return 1;
    }

    MP4Reader input(argv[1]);

    MP4Writer output(argv[2]);
    output.AddH264VideoTrack(input.GetVideoWidth(), input.GetVideoHeight(), input.GetVideoFps(), input.GetBitRate());

    unsigned char *sample = nullptr;
    unsigned int sample_size = 0;
    unsigned long long int duration = 0;
    bool is_key_frame = false;
    while (input.GetNextH264VideoSample(&sample, sample_size, duration, is_key_frame) == MP4Reader::MP4_READ_OK) {
        output.WriteH264VideoSample(sample, sample_size, is_key_frame, duration);
    }

    return 0;
}