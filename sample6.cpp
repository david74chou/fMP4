#include <string>
#include <thread>
#include <vector>

#include <mp4v2/mp4v2.h>
#include <netinet/in.h>

extern "C" {
#include <libavformat/avformat.h>
};

#define GST_USE_UNSTABLE_API /* To avoid H264 parser warning */
#include <gst/codecparsers/gsth264parser.h>

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
            , pSeqHeaders(nullptr)
            , pSeqHeaderSize(nullptr)
            , pPictHeaders(nullptr)
            , pPictHeaderSize(nullptr)
    {
        handle = MP4Read(this->file_path.c_str());

        video_track_id = MP4FindTrackId(handle, 0, MP4_VIDEO_TRACK_TYPE);
        if (video_track_id != MP4_INVALID_TRACK_ID) {
            video_timescale = MP4GetTrackTimeScale(handle, video_track_id);
            video_sample_max_size = MP4GetTrackMaxSampleSize(handle, video_track_id) * 2;
            video_duration = MP4GetTrackDuration(handle, video_track_id);
            video_sample = new unsigned char[video_sample_max_size];
            video_sample_number = MP4GetTrackNumberOfSamples(handle, video_track_id);

            if (MP4GetTrackH264SeqPictHeaders(handle,
                                              video_track_id,
                                              &pSeqHeaders,
                                              &pSeqHeaderSize,
                                              &pPictHeaders,
                                              &pPictHeaderSize))
            {
                printf("Get SPS(%d) and PPS(%d)\n", *pSeqHeaderSize, *pPictHeaderSize);

                for(int i = 0; (pSeqHeaders[i] && pSeqHeaderSize[i]); i++) {
                    printf("SPS(%d): %02x %02x %02x %02x %02x\n", i,
                           pSeqHeaders[i][0], pSeqHeaders[i][1], pSeqHeaders[i][2],
                           pSeqHeaders[i][3], pSeqHeaders[i][4]);
                }
                for(int i = 0; (pPictHeaders[i] && pPictHeaderSize[i]); i++) {
                    printf("PPS(%d): %02x %02x %02x %02x %02x\n", i,
                           pPictHeaders[i][0], pPictHeaders[i][1], pPictHeaders[i][2],
                           pPictHeaders[i][3], pPictHeaders[i][4]);
                }
            }
        }
    }

    ~MP4Reader()
    {
        if (pSeqHeaders || pSeqHeaderSize || pPictHeaders || pPictHeaderSize) {
            MP4FreeH264SeqPictHeaders(pSeqHeaders, pSeqHeaderSize, pPictHeaders, pPictHeaderSize);
        }

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

        unsigned int video_sample_offset = 0;
        if(MP4GetSampleSync(handle, video_track_id, next_video_sample_idx)) {
            /*
             * If current sample has key frame, we need to put SPS/PPS in front of key frame.
             */
            if (pSeqHeaders && pSeqHeaderSize) {
                for(int i = 0; (pSeqHeaders[i] && pSeqHeaderSize[i]); i++) {
                    (*(unsigned int *)(video_sample + video_sample_offset)) = htonl(1);
                    video_sample_offset += 4;
                    memcpy(video_sample + video_sample_offset, pSeqHeaders[i], pSeqHeaderSize[i]);
                    video_sample_offset += pSeqHeaderSize[i];
                }
            }
            if (pPictHeaders && pPictHeaderSize) {
                for(int i = 0; (pPictHeaders[i] && pPictHeaderSize[i]); i++) {
                    (*(unsigned int *)(video_sample + video_sample_offset)) = htonl(1);
                    video_sample_offset += 4;
                    memcpy(video_sample + video_sample_offset, pPictHeaders[i], pPictHeaderSize[i]);
                    video_sample_offset += pPictHeaderSize[i];
                }
            }
        }

        MP4Duration mp4_duration = 0;
        unsigned char *video_sample_start_addr = video_sample + video_sample_offset;
        sample_size = video_sample_max_size - video_sample_offset;
        if (!MP4ReadSample(handle, video_track_id, next_video_sample_idx,
                           &video_sample_start_addr, &sample_size,
                           NULL,
                           &mp4_duration,
                           NULL,
                           &is_key_frame)) {
            printf("Fail to read video sample (%d)\n", next_video_sample_idx);
            return MP4_READ_ERR;
        }

        // Convert AVC1 format to AnnexB
        if (sample_size >= 4) {
            unsigned int *p = (unsigned int *) video_sample_start_addr;
            *p = htonl(1);
        }

        *sample = video_sample;
        sample_size += video_sample_offset;
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
    unsigned char **pSeqHeaders;
    unsigned int *pSeqHeaderSize;
    unsigned char **pPictHeaders;
    unsigned int *pPictHeaderSize;
};

class MP4Writer
{
public:

    MP4Writer(const std::string &file_path)
            : file_path(file_path)
            , file_duration(0)
            , format_context(nullptr)
            , video_stream_id(0)
            , avio_buffer_size(1024 * 1024)
            , fptr(nullptr)
            , h264_parser(gst_h264_nal_parser_new())
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
            // Need to free the buffer that we allocate to our custom AVIOContext.
            if (format_context->pb->buffer)
                av_free(format_context->pb->buffer);

            // Free custom AVIOContext.
            if (format_context->pb)
                av_free(format_context->pb);

            // Close the file pointer
            if (fptr)
                fclose(fptr);
        }

        if (format_context)
            avformat_free_context(format_context);

        if (h264_parser)
            gst_h264_nal_parser_free(h264_parser);
    }

    // Performs a write operation using the signature required for avio.
    static int Write(void* opaque, uint8_t* buf, int buf_size)
    {
        static int i = 0;
        //printf("#%d Write: buf: %p(%02x%02x%02x%02x %c%c%c%c), size: %d\n", i++, buf, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf_size);
        return fwrite(buf, 1, buf_size, reinterpret_cast<MP4Writer*>(opaque)->fptr);
    }

    // Performs a seek operation using the signature required for avio.
    static int64_t Seek(void* opaque, int64_t offset, int whence)
    {
        static int i = 0;
        printf("#%d Seek: offset: %ld, whence: %d\n", i++, offset, whence);
        return 0;
    }

    bool WriteH264VideoSample(unsigned char *sample,
                              unsigned int sample_size,
                              bool is_key_frame,
                              unsigned long long int duration)
    {
        // Parse the sample into NALUs
        std::vector<GstH264NalUnit> nalus = ParseH264NALU(sample, sample_size);

        // To compatible with AVC1 format, we need to add SPS/PPS into mp4 header. (In avcC box)
        if (!format_context && is_key_frame) {
            GstH264NalUnit nal_sps = {0}, nal_pps = {0};
            for (auto nalu : nalus) {
                if (nalu.type == GST_H264_NAL_SPS) nal_sps = nalu;
                else if (nalu.type == GST_H264_NAL_PPS) nal_pps = nalu;
            }

            if (!AddH264VideoTrack(nal_sps, nal_pps)) {
                printf("Fail to add H264 video track\n");
                return false;
            }
        }

        // To compatible with AVC1 format, we could not put SPS/PPS in the sample.
        // So, we need to parse the data and only write video frame NALU into mp4.
        for (auto nalu : nalus) {
            if (nalu.type == GST_H264_NAL_SLICE_IDR || nalu.type == GST_H264_NAL_SLICE) {

                // Convert AnnexB format to AVC1
                unsigned int *p = (unsigned int *) (nalu.data + nalu.offset - 4);
                *p = htonl(nalu.size);

                AVPacket packet = { 0 };
                av_init_packet(&packet);

                packet.stream_index = video_stream_id;
                packet.data         = (unsigned char *)(p);
                packet.size         = nalu.size + 4;
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
            }
        }

        return true;
    }

private:

    bool AddH264VideoTrack(GstH264NalUnit &nal_sps, GstH264NalUnit &nal_pps)
    {
        // Parse SPS to get necessary params.
        unsigned char profile_idc = 0;
        unsigned char profile_compatibility = 0;
        unsigned char level_idc = 0;
        int width  = 0, height = 0;
        {
            GstH264SPS sps = {0};
            gst_h264_parser_parse_sps(h264_parser, &nal_sps, &sps, false);

            profile_idc = sps.profile_idc;
            profile_compatibility = (sps.constraint_set0_flag << 7) | (sps.constraint_set1_flag << 6) |
                                    (sps.constraint_set2_flag << 5) | (sps.constraint_set3_flag << 4);
            level_idc = sps.level_idc;
            width  = sps.frame_cropping_flag ? sps.crop_rect_width : sps.width;
            height = sps.frame_cropping_flag ? sps.crop_rect_height : sps.height;

            printf("Profile: %d, Compatibility: %d, Level: %d\n", profile_idc, profile_compatibility, level_idc);
            printf("Width: %d, Height: %d\n", width, height);
        }

        avformat_alloc_output_context2(&format_context, nullptr, "mp4", nullptr);
        if (!format_context) {
            printf("Fail to create output context\n");
            return false;
        }

        AVStream *out_stream = avformat_new_stream(format_context, nullptr);
        if (!out_stream) {
            printf("Fail to allocate output stream\n");
            return false;
        }

        out_stream->id = video_stream_id = format_context->nb_streams - 1;
        //out_stream->time_base = av_d2q(frame_rate, 100);
        //out_stream->codec->time_base = av_d2q(frame_rate, 100);
        out_stream->codec->codec_id   = AV_CODEC_ID_H264;
        out_stream->codec->profile    = profile_idc;
        out_stream->codec->level      = level_idc;
        out_stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        out_stream->codec->width      = width;
        out_stream->codec->height     = height;
        out_stream->codec->pix_fmt    = AV_PIX_FMT_YUV420P;
        out_stream->codec->codec_tag  = 0;

        // Fill extra data for AVCC format
        unsigned int extradata_offset = 0;
        out_stream->codec->extradata_size = nal_sps.size + nal_pps.size + 11;
        out_stream->codec->extradata = (uint8_t *)av_mallocz(out_stream->codec->extradata_size);
        out_stream->codec->extradata[extradata_offset++] = 0x01;                     // configurationVersion
        out_stream->codec->extradata[extradata_offset++] = profile_idc;              // AVCProfileIndication
        out_stream->codec->extradata[extradata_offset++] = profile_compatibility;    // profile_compatibility
        out_stream->codec->extradata[extradata_offset++] = level_idc;                // AVCLevelIndication, level: 4.0
        out_stream->codec->extradata[extradata_offset++] = 0xff;                     // 6 bits reserved (111111) + 2 bits nal size length - 1 (11)
        out_stream->codec->extradata[extradata_offset++] = 0xe1;                     // 3 bits reserved (111) + 5 bits number of sps (00001)

        unsigned short sps_size = static_cast<unsigned short>(nal_sps.size);
        *(unsigned short *)(out_stream->codec->extradata + extradata_offset) = htons(sps_size);
        extradata_offset += 2;
        memcpy(out_stream->codec->extradata + extradata_offset, nal_sps.data + nal_sps.offset, nal_sps.size);
        extradata_offset += nal_sps.size;

        out_stream->codec->extradata[extradata_offset++] = 0x01;                     // 8 bits number of pps (00000000)
        unsigned short pps_size = static_cast<unsigned short>(nal_pps.size);
        *(unsigned short *)(out_stream->codec->extradata + extradata_offset) = htons(pps_size);
        extradata_offset += 2;
        memcpy(out_stream->codec->extradata + extradata_offset, nal_pps.data + nal_pps.offset, nal_pps.size);
        extradata_offset += nal_pps.size;

        if (format_context->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

        av_dump_format(format_context, 0, "CustomAVIO", 1);

        /*
         * Open output file
         */
        {
            if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
                // Allocate our custom AVIO context
                AVIOContext *avio_out = avio_alloc_context(static_cast<unsigned char *>(av_malloc(avio_buffer_size)),
                                                           avio_buffer_size,
                                                           1,
                                                           this,
                                                           nullptr,
                                                           &Write,
                                                           nullptr
                );
                if(!avio_out) {
                    printf("Fail to create avio context\n");
                    return false;
                }

                format_context->pb = avio_out;
                format_context->flags = AVFMT_FLAG_CUSTOM_IO;

                fptr = fopen(file_path.c_str(), "wb+");
            }
        }

        /*
         * Write file header
         */
        {
            AVDictionary *movflags = nullptr;
            av_dict_set(&movflags, "movflags", "empty_moov+default_base_moof+frag_keyframe", 0);
            if (avformat_write_header(format_context, &movflags) < 0) {
                printf("Error occurred when opening output file\n");
                return false;
            }
            av_dict_free(&movflags);
        }

        return true;
    }

    std::vector<GstH264NalUnit> ParseH264NALU(unsigned char *data, unsigned int length)
    {
        std::vector<GstH264NalUnit> nalus;

        GstH264NalUnit nalu = {0};
        GstH264ParserResult result;
        unsigned int offset = 0;
        while ((result = gst_h264_parser_identify_nalu(h264_parser, data, offset, length, &nalu)) == GST_H264_PARSER_OK)
        {
            // Update the offset
            gst_h264_parser_parse_nal(h264_parser, &nalu);
            offset = nalu.size + nalu.offset;

            nalus.push_back(nalu);
        }

        // Handle the last NALU because there is no other start_code followed it.
        if (gst_h264_parser_identify_nalu_unchecked(h264_parser, data, offset, length, &nalu) == GST_H264_PARSER_OK) {

            gst_h264_parser_parse_nal(h264_parser, &nalu);

            nalus.push_back(nalu);
        }

        return nalus;
    }

    const std::string file_path;
    unsigned long long int file_duration;
    AVFormatContext *format_context;
    unsigned int video_stream_id;
    unsigned int avio_buffer_size;
    FILE *fptr;
    GstH264NalParser *h264_parser;
};

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: %s input output\n", argv[0]);
        return 1;
    }

    MP4Writer output(argv[argc - 1]);

    int i = 1;
    do {
        std::shared_ptr<MP4Reader> input = std::make_shared<MP4Reader>(argv[i]);
        printf("#%d: %s\n", i, argv[i]);

        unsigned char *sample = nullptr;
        unsigned int sample_size = 0;
        unsigned long long int duration = 0;
        bool is_key_frame = false;
        while (input->GetNextH264VideoSample(&sample, sample_size, duration, is_key_frame) == MP4Reader::MP4_READ_OK) {
            output.WriteH264VideoSample(sample, sample_size, is_key_frame, duration);
        }

        i++;
    } while (i < argc - 1);

    return 0;
}