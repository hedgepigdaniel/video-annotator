#ifndef _AV_FRAME_SOURCE_FILE_VAAPI_HPP_
#define _AV_FRAME_SOURCE_FILE_VAAPI_HPP_


#include "AvFrameSource.hpp"

#include <string>

/**
 * Reads frames from a video file
 */
class AvFrameSourceFileVaapi: public AvFrameSource {
    AVBufferRef *vaapi_device_ctx = NULL;
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
    AvFrameSourceFileVaapi(std::string file_path, AVBufferRef *vaapi_device_ctx);
    AVFrame* pull_frame();
    AVFrame* peek_frame();
};

#endif // _AV_FRAME_SOURCE_FILE_VAAPI_HPP_