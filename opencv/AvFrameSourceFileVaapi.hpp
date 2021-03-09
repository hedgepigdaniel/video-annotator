#ifndef _AV_FRAME_SOURCE_FILE_VAAPI_HPP_
#define _AV_FRAME_SOURCE_FILE_VAAPI_HPP_


#include "AvFrameSource.hpp"

#include <string>
#include <memory>

/**
 * Reads VAAPI backed `AVFrame`s from a video file
 */
class AvFrameSourceFileVaapi: public AvFrameSource {
    std::shared_ptr<AVBufferRef> vaapi_device_ctx;
    AVFormatContext *format_ctx = NULL;
    int video_stream = -1;
    int gpmf_stream = -1;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    AVFrame *next_frame = NULL;
    AVPacket packet;
    bool input_ended = false;

    void read_input_packet();
  public:
    AvFrameSourceFileVaapi(std::string file_path, std::shared_ptr<AVBufferRef> vaapi_device_ctx);
    AVFrame* pull_frame();
    AVFrame* peek_frame();
    ~AvFrameSourceFileVaapi();
};

#endif // _AV_FRAME_SOURCE_FILE_VAAPI_HPP_