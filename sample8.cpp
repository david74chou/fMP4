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

#define BUFFER_SIZE (1024 * 1024)

static const std::string av_make_error_string(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return (std::string)errbuf;
}

#undef av_err2str
#define av_err2str(errnum) av_make_error_string(errnum).c_str()

class CircularBuffer
{
public:
    CircularBuffer(unsigned int capacity);
    ~CircularBuffer();

    unsigned int size() const { return size_; }
    unsigned int capacity() const { return capacity_; }
    // Return number of bytes written.
    unsigned int write(const char *data, unsigned int bytes);
    // Return number of bytes read.
    unsigned int read(unsigned char *data, unsigned int bytes);

private:
    unsigned int beg_index_, end_index_, size_, capacity_;
    unsigned char *data_;
};

CircularBuffer::CircularBuffer(unsigned int capacity)
        : beg_index_(0)
        , end_index_(0)
        , size_(0)
        , capacity_(capacity)
{
    data_ = new unsigned char[capacity];
}

CircularBuffer::~CircularBuffer()
{
    delete [] data_;
}

unsigned int CircularBuffer::write(const char *data, unsigned int bytes)
{
    if (bytes == 0) return 0;

    unsigned int capacity = capacity_;
    unsigned int bytes_to_write = std::min(bytes, capacity - size_);

    // Write in a single step
    if (bytes_to_write <= capacity - end_index_)
    {
        memcpy(data_ + end_index_, data, bytes_to_write);
        end_index_ += bytes_to_write;
        if (end_index_ == capacity) end_index_ = 0;
    }
        // Write in two steps
    else
    {
        unsigned int size_1 = capacity - end_index_;
        memcpy(data_ + end_index_, data, size_1);
        unsigned int size_2 = bytes_to_write - size_1;
        memcpy(data_, data + size_1, size_2);
        end_index_ = size_2;
    }

    size_ += bytes_to_write;
    return bytes_to_write;
}

unsigned int CircularBuffer::read(unsigned char *data, unsigned int bytes)
{
    if (bytes == 0) return 0;

    unsigned int capacity = capacity_;
    unsigned int bytes_to_read = std::min(bytes, size_);

    // Read in a single step
    if (bytes_to_read <= capacity - beg_index_)
    {
        memcpy(data, data_ + beg_index_, bytes_to_read);
        beg_index_ += bytes_to_read;
        if (beg_index_ == capacity) beg_index_ = 0;
    }
        // Read in two steps
    else
    {
        unsigned int size_1 = capacity - beg_index_;
        memcpy(data, data_ + beg_index_, size_1);
        unsigned int size_2 = bytes_to_read - size_1;
        memcpy(data + size_1, data_, size_2);
        beg_index_ = size_2;
    }

    size_ -= bytes_to_read;
    return bytes_to_read;
}

class fMP4Demuxer
{
public:

    fMP4Demuxer(const std::string &output_file_path)
            : output_file_path(output_file_path)
            , file_duration(0)
            , format_context(nullptr)
            , video_stream_id(0)
            , h264_parser(gst_h264_nal_parser_new())
            , buffer(BUFFER_SIZE)
            , is_opened(false)
    {
        av_register_all();
    }

    ~fMP4Demuxer()
    {
        /*if (format_context && format_context->streams[video_stream_id]) {
            if (format_context->streams[video_stream_id]->codec) {
                avcodec_close(format_context->streams[video_stream_id]->codec);
            }
        }*/

        /*if (format_context && !(format_context->oformat->flags & AVFMT_NOFILE)) {
            // Need to free the buffer that we allocate to our custom AVIOContext.
            if (format_context->pb->buffer)
                av_free(format_context->pb->buffer);

            // Free custom AVIOContext.
            if (format_context->pb)
                av_free(format_context->pb);
        }

        if (format_context)
            avformat_free_context(format_context);

        if (h264_parser)
            gst_h264_nal_parser_free(h264_parser);*/
    }

    bool Init()
    {
        format_context = avformat_alloc_context();

        // Allocate our custom AVIO context
        AVIOContext *avio_in = avio_alloc_context(static_cast<unsigned char *>(av_malloc(BUFFER_SIZE)),
                                                  BUFFER_SIZE,
                                                  0,
                                                  this,
                                                  &Read,
                                                  nullptr,
                                                  nullptr
        );
        if(!avio_in) {
            printf("Fail to create avio context\n");
            return false;
        }
        //avio_in->seekable = 0;
        format_context->pb = avio_in;
        format_context->flags = AVFMT_FLAG_CUSTOM_IO;

        /*for (int i = 0; i < format_context->nb_streams; i++) {
            AVStream *stream;
            AVCodecContext *codec_ctx;
            stream = ifmt_ctx->streams[i];
            codec_ctx = stream->codec;
            *//* Reencode video & audio and remux subtitles etc. *//*
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO){
                *//* Open decoder *//*
                ret = avcodec_open2(codec_ctx,
                                    avcodec_find_decoder(codec_ctx->codec_id), NULL);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                    return ret;
                }
            }
        }*/

        //av_dump_format(format_context, 0, "whatever", 0);

        return true;
    }

    // Performs a read operation using the signature required for avio.
    static int Read(void* opaque, unsigned char* buffer, int buffer_size)
    {
        fMP4Demuxer *demuxer = reinterpret_cast<fMP4Demuxer*>(opaque);

        printf("Read: ->\n");
        printf("buffer: %p, buffer_size: %d, data_buffer_size: %d\n", (void *)buffer, buffer_size, demuxer->buffer.size());

        int read_size = std::min((unsigned int)buffer_size, demuxer->buffer.size());

        /* copy internal buffer data to buf */
        demuxer->buffer.read(buffer, read_size);

//        bd->ptr  += buf_size;
//        bd->size -= buf_size;

        printf("Read: <- read_size: %d(0x%x)\n", read_size, read_size);
        return read_size;
    }

    bool FeedSample(unsigned char *sample, unsigned int sample_size)
    {
        printf("FeedSample -> sample_size: %d\n", sample_size);
        buffer.write((char *)sample, sample_size);

        int ret = 0;
        if (!is_opened && buffer.size() > 2048) {
            printf("avformat_open_input ->\n");
            if ((ret = avformat_open_input(&format_context, nullptr, nullptr, nullptr)) >= 0) {
                printf("avformat_open_input <- Success to open input file\n");

                /*printf("avformat_find_stream_info ->\n");
                if ((ret = avformat_find_stream_info(format_context, NULL)) >= 0) {
                    printf("avformat_find_stream_info <- Success to find stream information\n");
                } else {
                    printf("avformat_find_stream_info <- Fail to find stream information\n");
                }*/

                is_opened = true;
            } else {
                printf("avformat_open_input <- Fail to open input file\n");
            }

        }

        if (is_opened) {
            printf("AVIO eof: %d, error: %d\n", format_context->pb->eof_reached, format_context->pb->error);
            printf("buffer: %p, buffer_size: 0x%x\n", (void *)format_context->pb->buffer, format_context->pb->buffer_size);
            printf("buf_ptr: %p, buf_end: %p\n", (void *)format_context->pb->buf_ptr, (void *)format_context->pb->buf_end);
            if (format_context->pb->eof_reached) {
                format_context->pb->eof_reached = 0;
                format_context->pb->error = 0;
                format_context->pb->buf_ptr = format_context->pb->buffer;
            }

            while(true) {
                AVPacket packet = {0};
                av_init_packet(&packet);
                printf("av_read_frame ->\n");
                if ((ret = av_read_frame(format_context, &packet)) < 0) {
                    printf("avio_feof: %d, eof_reached: %d\n", avio_feof(format_context->pb), format_context->pb->eof_reached);
                    printf("av_read_frame <- Fail, ret: %s\n", av_err2str(ret));
                    break;
                }
                printf("av_read_frame <- Success\n");
                printf("data: %p, size: %d, duration: %d, flags: %d\n", (void *)packet.data, packet.size, packet.duration, packet.flags);
                av_packet_unref(&packet);
            }
        }

        printf("FeedSample <-\n");
        return true;
    }

private:

    const std::string output_file_path;

    CircularBuffer buffer;

    bool is_opened;
    unsigned long long int file_duration;
    AVFormatContext *format_context;
    unsigned int video_stream_id;
    GstH264NalParser *h264_parser;
};

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: %s input-dir output-file\n", argv[0]);
        return 1;
    }

    unsigned int size_read = 0;
    unsigned char buffer[BUFFER_SIZE];

    std::shared_ptr<fMP4Demuxer> demuxer = std::make_shared<fMP4Demuxer>(argv[2]);
    if (!demuxer->Init()) {
        return 1;
    }

    FILE *fptr = nullptr;
    for (int i = 0; (fptr = fopen((std::string(argv[1]) + std::string("frag-") + std::to_string(i)).c_str(), "rb")) != nullptr; i++) {
        size_read = fread(buffer, 1, 1024 * 1024, fptr);
        printf("\nRead frag: %d\n", i);
        if (!demuxer->FeedSample(buffer, size_read)) {
            printf("Fail to feed sample into demuxer\n");
            fclose(fptr);
            break;
        }
        fclose(fptr);
    }

    return 0;
}