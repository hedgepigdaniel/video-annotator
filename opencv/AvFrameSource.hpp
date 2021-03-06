#ifndef _AV_FRAME_SOURCE_HPP_
#define _AV_FRAME_SOURCE_HPP_

extern "C" {
    #include <libavformat/avformat.h>
}

/**
 * A source of `AVFrame *`s
 */
class AvFrameSource {
  public:
    /**
     * Return next frame and advance the current position
     * Raises exception if there is no available frame
     */
    virtual AVFrame* pull_frame() = 0;

    /**
     * Return next frame but not not advance the position
     * Raises exception if there is no available frame
     */
    virtual AVFrame* peek_frame() = 0;

    virtual ~AvFrameSource() = default;
};

#endif // _AV_FRAME_SOURCE_HPP_
