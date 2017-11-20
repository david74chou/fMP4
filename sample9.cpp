#include <string>
#include <thread>
#include <vector>

#include <mp4v2/mp4v2.h>
#include <netinet/in.h>

#include <ap4/Ap4.h>

#define GST_USE_UNSTABLE_API /* To avoid H264 parser warning */
#include <gst/codecparsers/gsth264parser.h>

#define MP4_DEFAULT_VIDEO_TRACK_ID  1
#define MP4_DEFAULT_TRACK_TIMESCALE 9000
#define MP4_DEFAULT_MOVIE_TIMESCALE 1000

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
                printf("VideoTrack: Get SPS(%d) and PPS(%d)\n", *pSeqHeaderSize, *pPictHeaderSize);

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
        unsigned char *tmp_addr = video_sample_start_addr;
        if (sample_size >= 4) {
            while((tmp_addr[4] & 0x1F) != GST_H264_NAL_SLICE_IDR &&
                  (tmp_addr[4] & 0x1F) != GST_H264_NAL_SLICE)
            {
                unsigned int *p = (unsigned int *) tmp_addr;
                unsigned int header_size = ntohl(*p);
                *p = htonl(1);

                tmp_addr += (header_size + 4);
            }

            unsigned int *p = (unsigned int *) tmp_addr;
            *p = htonl(1);
        }

        *sample = video_sample;
        sample_size += video_sample_offset;
        duration = (1000 * mp4_duration) / video_timescale;
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

class FileOutputStream : public AP4_ByteStream
{
public:
    FileOutputStream(const std::string &file_path, bool is_open_new_file)
        : fptr(nullptr)
        , reference_count(1)
        , position(0)
    {
        fptr = fopen(file_path.c_str(), is_open_new_file ? "wb" : "ab");
    }

    // AP4_ByteStream methods
    AP4_Result WritePartial(const void* buffer,
                            AP4_Size    buf_size,
                            AP4_Size&   bytes_written)
    {
        const unsigned char *buf = (const unsigned char *)buffer;
        static int i = 0;
        if (buf_size >=4) {
            if (isprint(buf[0]) && isprint(buf[1]) && isprint(buf[2]) && isprint(buf[3])) {
                printf("#%d Write: buf: %p(%c%c%c%c), size: %d\n", i++,
                       buffer, buf[0], buf[1], buf[2], buf[3], buf_size);
            }
        }

        bytes_written = buf_size;
        position += bytes_written;

        /*if (buf[4] == 'm' && buf[5] == 'f' && buf[6] == 'r' && buf[7] == 'a') {
            return AP4_SUCCESS;
        } else*/ {
            fwrite(buf, 1, buf_size, fptr);
        }

        return AP4_SUCCESS;
    }

    AP4_Result Tell(AP4_Position& position)
    {
        position = this->position;
        return AP4_SUCCESS;
    }

    AP4_Result ReadPartial(void* buffer, AP4_Size  bytes_to_read, AP4_Size& bytes_read) { printf("ReadPartial\n"); return AP4_ERROR_NOT_SUPPORTED; }
    AP4_Result Seek(AP4_Position position)  { printf("Seek\n"); return AP4_ERROR_NOT_SUPPORTED; }
    AP4_Result GetSize(AP4_LargeSize& size) { printf("GetSize\n"); return AP4_ERROR_NOT_SUPPORTED; }

    // AP4_Referenceable methods
    void AddReference() { reference_count++; }
    void Release() { if (--reference_count == 0) delete this; }

protected:

    ~FileOutputStream() { if (fptr) fclose(fptr); }

private:

    FILE *fptr;
    unsigned int reference_count;
    unsigned long long int position;
};

class MP4Writer : public AP4_FeedSegmentBuilder
{
public:

    MP4Writer(const std::string &file_path, bool is_open_new_file)
        : AP4_FeedSegmentBuilder(AP4_Track::TYPE_VIDEO, MP4_DEFAULT_VIDEO_TRACK_ID)
        , file_path(file_path)
        , is_open_new_file(is_open_new_file)
        , sequence_number(0)
        , h264_parser(gst_h264_nal_parser_new())
    {
        m_Timescale = MP4_DEFAULT_TRACK_TIMESCALE;
    }

    ~MP4Writer()
    {
        if (file_output_stream)
            file_output_stream->Release();

        if (h264_parser)
            gst_h264_nal_parser_free(h264_parser);
    }

    bool WriteH264VideoSample(unsigned char *sample,
                              unsigned int sample_size,
                              bool is_key_frame,
                              unsigned long long int duration)
    {
        printf("WriteH264VideoSample -> \n");

        // Parse the sample into NALUs
        std::vector<GstH264NalUnit> nalus = ParseH264NALU(sample, sample_size);

        // Write init segment
        if (!file_output_stream) {
            if (is_key_frame) {
                for (auto nalu : nalus) {
                    if (nalu.type == GST_H264_NAL_SPS) nal_sps = nalu;
                    else if (nalu.type == GST_H264_NAL_PPS) nal_pps = nalu;
                }
                file_output_stream = new FileOutputStream(file_path, is_open_new_file);

                if (file_output_stream) {
                    WriteInitSegment(nal_sps, nal_pps, *file_output_stream);
                }
            }
        }

        // To compatible with AVC1 format, we could not put SPS/PPS in the sample.
        // So, we need to parse the data and only write video frame NALU into mp4.
        AP4_Result result;
        unsigned int byte_consumed = 0;
        for (auto nalu : nalus) {
            if (nalu.type == GST_H264_NAL_SLICE_IDR || nalu.type == GST_H264_NAL_SLICE) {
                if (!Feed(nalu.data + nalu.offset, nalu.size, is_key_frame, duration)) {
                    printf("ERROR: Feed() failed (%d)\n", result);
                    break;
                }
                if (file_output_stream) {
                    WriteMediaSegment(*file_output_stream, ++sequence_number);
                }
            }
        }

        printf("WriteH264VideoSample <- \n\n");
        return true;
    }

private:

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

    // Overwrite the original WriteMediaSegment() in AP4_FeedSegmentBuilder with our own implementation
    virtual AP4_Result WriteMediaSegment(AP4_ByteStream& stream, unsigned int sequence_number)
    {
        unsigned int tfhd_flags = AP4_TFHD_FLAG_DEFAULT_BASE_IS_MOOF;
        if (m_TrackType == AP4_Track::TYPE_VIDEO) {
            tfhd_flags |= AP4_TFHD_FLAG_DEFAULT_SAMPLE_FLAGS_PRESENT;
        }

        // setup the moof structure
        AP4_ContainerAtom* moof = new AP4_ContainerAtom(AP4_ATOM_TYPE_MOOF);
        AP4_MfhdAtom* mfhd = new AP4_MfhdAtom(sequence_number);
        moof->AddChild(mfhd);
        AP4_ContainerAtom* traf = new AP4_ContainerAtom(AP4_ATOM_TYPE_TRAF);
        AP4_TfhdAtom* tfhd = new AP4_TfhdAtom(tfhd_flags,
                                              m_TrackId,
                                              0,
                                              1,
                                              0,
                                              0,
                                              0);
        if (tfhd_flags & AP4_TFHD_FLAG_DEFAULT_SAMPLE_FLAGS_PRESENT) {
            tfhd->SetDefaultSampleFlags(0x1010000); // sample_is_non_sync_sample=1, sample_depends_on=1 (not I frame)
        }

        traf->AddChild(tfhd);
        AP4_TfdtAtom* tfdt = new AP4_TfdtAtom(1, m_MediaTimeOrigin+m_MediaStartTime);
        traf->AddChild(tfdt);
        AP4_UI32 trun_flags = AP4_TRUN_FLAG_DATA_OFFSET_PRESENT     |
                              AP4_TRUN_FLAG_SAMPLE_DURATION_PRESENT |
                              AP4_TRUN_FLAG_SAMPLE_SIZE_PRESENT     |
                              AP4_TRUN_FLAG_SAMPLE_FLAGS_PRESENT;
        AP4_TrunAtom* trun = new AP4_TrunAtom(trun_flags, 0, 0);

        traf->AddChild(trun);
        moof->AddChild(traf);

        // add samples to the fragment
        AP4_Array<AP4_UI32>            sample_indexes;
        AP4_Array<AP4_TrunAtom::Entry> trun_entries;
        AP4_UI32                       mdat_size = AP4_ATOM_HEADER_SIZE;
        trun_entries.SetItemCount(m_Samples.ItemCount());
        for (unsigned int i=0; i<m_Samples.ItemCount(); i++) {
            // if we have one non-zero CTS delta, we'll need to express it
            if (m_Samples[i].GetCtsDelta()) {
                trun->SetFlags(trun->GetFlags() | AP4_TRUN_FLAG_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT);
            }

            // add one sample
            AP4_TrunAtom::Entry& trun_entry = trun_entries[i];
            trun_entry.sample_duration                = m_Samples[i].GetDuration();
            trun_entry.sample_size                    = m_Samples[i].GetSize();
            trun_entry.sample_composition_time_offset = m_Samples[i].GetCtsDelta();
            trun_entry.sample_flags = m_Samples[i].IsSync() ? 0x02000000 : 0x01010000;

            mdat_size += trun_entry.sample_size;
        }

        // update moof and children
        trun->SetEntries(trun_entries);
        trun->SetDataOffset((AP4_UI32)moof->GetSize()+AP4_ATOM_HEADER_SIZE);

        // write moof
        moof->Write(stream);

        // write mdat
        stream.WriteUI32(mdat_size);
        stream.WriteUI32(AP4_ATOM_TYPE_MDAT);
        for (unsigned int i=0; i<m_Samples.ItemCount(); i++) {
            AP4_Result result;
            AP4_ByteStream* data_stream = m_Samples[i].GetDataStream();
            result = data_stream->Seek(m_Samples[i].GetOffset());
            if (AP4_FAILED(result)) {
                data_stream->Release();
                return result;
            }
            result = data_stream->CopyTo(stream, m_Samples[i].GetSize());
            if (AP4_FAILED(result)) {
                data_stream->Release();
                return result;
            }

            data_stream->Release();
        }

        // update counters
        m_SampleStartNumber += m_Samples.ItemCount();
        m_MediaStartTime    += m_MediaDuration;
        m_MediaDuration      = 0;

        // cleanup
        delete moof;
        m_Samples.Clear();

        return AP4_SUCCESS;
    }

    // We use our own WriteInitSegment() and Feed() because we need more parameters than the original ones.
    bool WriteInitSegment(GstH264NalUnit &nal_sps, GstH264NalUnit &nal_pps, AP4_ByteStream &stream)
    {
        // Parse SPS to get necessary params.
        GstH264SPS sps = {0};
        unsigned int video_width  = 0, video_height = 0;
        {
            gst_h264_parser_parse_sps(h264_parser, &nal_sps, &sps, false);
            video_width  = (unsigned int)(sps.frame_cropping_flag ? sps.crop_rect_width : sps.width);
            video_height = (unsigned int)(sps.frame_cropping_flag ? sps.crop_rect_height : sps.height);
        }

        AP4_Result result;

        // collect the SPS and PPS into arrays
        AP4_Array<AP4_DataBuffer> sps_array;
        sps_array.Append(AP4_DataBuffer(nal_sps.data + nal_sps.offset, nal_sps.size));
        AP4_Array<AP4_DataBuffer> pps_array;
        pps_array.Append(AP4_DataBuffer(nal_pps.data + nal_pps.offset, nal_pps.size));

        // setup the video the sample descripton
        AP4_AvcSampleDescription* sample_description =
                new AP4_AvcSampleDescription(AP4_SAMPLE_FORMAT_AVC1,
                                             (AP4_UI16)video_width,
                                             (AP4_UI16)video_height,
                                             24,
                                             "",
                                             sps.profile_idc,
                                             sps.level_idc,
                                             0,
                                             4,
                                             sps_array,
                                             pps_array);

        // create the output file object
        AP4_Movie* output_movie = new AP4_Movie(MP4_DEFAULT_MOVIE_TIMESCALE);

        // create an mvex container
        AP4_ContainerAtom* mvex = new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX);
        //AP4_MehdAtom* mehd = new AP4_MehdAtom(0);
        //mvex->AddChild(mehd);

        // create a sample table (with no samples) to hold the sample description
        AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();
        sample_table->AddSampleDescription(sample_description, true);

        // create the track
        AP4_Track* output_track = new AP4_Track(AP4_Track::TYPE_VIDEO,
                                                sample_table,
                                                m_TrackId,
                                                MP4_DEFAULT_MOVIE_TIMESCALE,
                                                0,
                                                m_Timescale,
                                                0,
                                                m_TrackLanguage.GetChars(),
                                                video_width << 16,
                                                video_height << 16);
        output_movie->AddTrack(output_track);

        // add a trex entry to the mvex container
        AP4_TrexAtom* trex = new AP4_TrexAtom(m_TrackId,
                                              1,
                                              0,
                                              0,
                                              0);
        mvex->AddChild(trex);

        // update the mehd duration
        // TBD mehd->SetDuration(0);

        // the mvex container to the moov container
        output_movie->GetMoovAtom()->AddChild(mvex);

        // write the ftyp atom
        AP4_Array<AP4_UI32> brands;
        brands.Append(AP4_FILE_BRAND_ISO6);
        brands.Append(AP4_FILE_BRAND_MP41);

        AP4_FtypAtom* ftyp = new AP4_FtypAtom(AP4_FILE_BRAND_ISO5, 512, &brands[0], brands.ItemCount());
        ftyp->Write(stream);
        delete ftyp;

        // write the moov atom
        result = output_movie->GetMoovAtom()->Write(stream);
        if (AP4_FAILED(result)) {
            return false;
        }

        // cleanup
        delete output_movie;

        return true;
    }
    bool Feed(const unsigned char *data,
              unsigned int data_size,
              bool is_key_frame,
              unsigned long long int duration)
    {
        // format the sample data
        AP4_MemoryByteStream* sample_data = new AP4_MemoryByteStream(data_size);
        {
            sample_data->WriteUI32(data_size);
            sample_data->Write(data, data_size);

            /*
             * Sometimes we might encounter frames with duration == 0.
             * In this case, hard-code it to 50ms
             */
            if (!duration)
                duration = 50;

            // compute the duration in timescale
            AP4_UI32 timescale_duration = (AP4_UI32)(AP4_ConvertTime(duration, 1000, m_Timescale));
            AP4_UI64 timescale_dts      = m_MediaStartTime;

            // create a new sample and add it to the list
            AP4_Sample sample(*sample_data, 0, data_size + 4, timescale_duration, 0, timescale_dts, 0, is_key_frame);
            AddSample(sample);
        }
        sample_data->Release();

        return true;
    }

    // These two functions are dummy implement for AP4_FeedSegmentBuilder, but we never use them.
    virtual AP4_Result WriteInitSegment(AP4_ByteStream &stream) { return AP4_SUCCESS; }
    virtual AP4_Result Feed(const void *data, unsigned int data_size, unsigned int &bytes_consumed) { return AP4_SUCCESS; }

    FileOutputStream *file_output_stream;
    std::string file_path;
    bool is_open_new_file;
    unsigned int sequence_number;

    GstH264NalParser *h264_parser;
    GstH264NalUnit nal_sps;
    GstH264NalUnit nal_pps;
};

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: %s input output\n", argv[0]);
        return 1;
    }

    bool is_open_new_file = true;
    int i = 1;
    do {
        std::shared_ptr<MP4Writer> output = std::make_shared<MP4Writer>(argv[argc - 1], is_open_new_file);
        std::shared_ptr<MP4Reader> input = std::make_shared<MP4Reader>(argv[i]);
        printf("#%d: %s\n", i, argv[i]);

        unsigned char *sample = nullptr;
        unsigned int sample_size = 0;
        unsigned long long int duration = 0;
        bool is_key_frame = false;
        while (input->GetNextH264VideoSample(&sample, sample_size, duration, is_key_frame) == MP4Reader::MP4_READ_OK) {
            output->WriteH264VideoSample(sample, sample_size, is_key_frame, duration);
        }

        i++;
        is_open_new_file = false;
    } while (i < argc - 1);

    return 0;
}