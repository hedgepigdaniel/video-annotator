#ifndef _AV_FRAME_SOURCE_OPENCL_HPP_
#define _AV_FRAME_SOURCE_OPENCL_HPP_

extern "C" {
    #include <libavformat/avformat.h>
}


#include "FrameSource.hpp"

/**
 * Reads frames from a video file
 */
class AvFrameSourceOpenCl: public FrameSource {
    AVBufferRef *vaapi_device_ctx = NULL;
    AVBufferRef *ocl_device_ctx = NULL;
    AVFormatContext *format_ctx = NULL;
    int video_stream = -1;
    int gpmf_stream = -1;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    AVFrame *next_frame = NULL;
    AVPacket packet;
    bool input_ended = false;

    void open_input_file(std::string file_path);
    void read_input_packet();
    AVFrame* opencl_frame_from_vaapi_frame(AVFrame *vaapi_frame);
  public:
    AvFrameSourceOpenCl(std::string file_path);

    /**
     * Return next frame and advance the current position
     * Raises exception if there is no available frame
     */
    AVFrame* pull_frame();

    /**
     * Return next frame but not not advance the position
     * Raises exception if there is no available frame
     */
    AVFrame* peek_frame();

    static bool is_supported();
};

#endif // _AV_FRAME_SOURCE_OPENCL_HPP_