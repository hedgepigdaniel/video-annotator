#include "AvFrameSourceOpenCl.hpp"

#include <iostream>

#include "utils.hpp"

using namespace std;

int get_gpmf_stream_id(AVFormatContext *format_ctx) {
    int result = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        AVStream *stream = format_ctx->streams[i];
        AVDictionaryEntry *entry = av_dict_get(stream->metadata, "handler_name", NULL, 0);
        if (entry != NULL && strcmp(entry->value, "	GoPro MET") == 0) {
            result = i;
            break;
        }
    }
    return result;
}

static enum AVPixelFormat get_vaapi_format(
    AVCodecContext *ctx,
    const enum AVPixelFormat *pix_fmts
) {
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_VAAPI)
            return *p;
    }
    cerr << "Unable to decode this file using VA-API.\n";
    return AV_PIX_FMT_NONE;
}

int has_hwaccel_support(enum AVHWDeviceType type) {
    enum AVHWDeviceType current = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);
    while (current != AV_HWDEVICE_TYPE_NONE) {
        if (current == type) {
            return true;
        }
        current = av_hwdevice_iterate_types(current);
    }
    return false;
}

AvFrameSourceOpenCl::AvFrameSourceOpenCl(std::string file_path) {
    int ret;
    ret = av_hwdevice_ctx_create(
        &this->vaapi_device_ctx,
        AV_HWDEVICE_TYPE_VAAPI,
        NULL,
        NULL,
        0
    );
    if (ret < 0) {
        cerr << "Failed to create a VAAPI device context:"<< errString(ret) << "\n";
        throw ret;
    }

    ret = av_hwdevice_ctx_create_derived(
        &this->ocl_device_ctx,
        AV_HWDEVICE_TYPE_OPENCL,
        this->vaapi_device_ctx,
        0
    );
    if (ret < 0) {
        cerr << "Failed to map VAAPI device to OpenCL device:" << errString(ret) << "\n";
        throw ret;
    }

    this->open_input_file(file_path);
}

void AvFrameSourceOpenCl::open_input_file(std::string file_path) {
    int err;
    AVStream *video = NULL;

    err = avformat_open_input(&this->format_ctx, file_path.c_str(), NULL, NULL);
    if (err) {
        cerr << "Failed to map hwframes context to OpenCL:" << errString(err) << "\n";
        throw err;
    }

    err = avformat_find_stream_info(this->format_ctx, NULL);
    if (err < 0) {
        cerr << "Failed to find input stream information:" << errString(err) << "\n";
        throw err;
    }

    this->video_stream = av_find_best_stream(
        this->format_ctx,
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        &this->decoder,
        0
    );
    if (this->video_stream < 0) {
        cerr << "Failed to find a video stream in the input file:" <<
            errString(this->video_stream) << "\n";
        throw this->video_stream;
    }

    this->gpmf_stream = get_gpmf_stream_id(this->format_ctx);
    if (this->gpmf_stream != -1) {
        cerr << "Found GoPro metadata stream in input\n";
    }

    this->decoder_ctx = avcodec_alloc_context3(this->decoder);
    if (!this->decoder_ctx) {
        err = AVERROR(ENOMEM);
        cerr << "Failed to allocate decoder context:" << errString(err) << "\n";
        throw err;
    }

    video = this->format_ctx->streams[this->video_stream];
    err = avcodec_parameters_to_context(this->decoder_ctx, video->codecpar);
    if (err < 0) {
        cerr << "avcodec_parameters_to_context error:" << errString(err) << "\n";
        throw err;
    }

    this->decoder_ctx->hw_device_ctx = av_buffer_ref(this->vaapi_device_ctx);
    if (!this->decoder_ctx->hw_device_ctx) {
        err = AVERROR(ENOMEM);
        cerr << "Failed to reference VAAPI hardware context:" << errString(err) << "\n";
        throw err;
    }

    this->decoder_ctx->get_format = get_vaapi_format;

    err = avcodec_open2(this->decoder_ctx, this->decoder, NULL);
    if (err < 0) {
        cerr << "Failed to open codec for decoding:" << errString(err) << "\n";
        throw err;
    }
}

void AvFrameSourceOpenCl::read_input_packet() {
    int err;
    err = av_read_frame(this->format_ctx, &this->packet);
    if (err < 0) {
        this->input_ended = true;
        return;
    }

    if (packet.stream_index == this->video_stream) {
        err = avcodec_send_packet(this->decoder_ctx, &packet);
        if (err < 0) {
            cerr << "Failed to decode frame:" << errString(err) << "\n";
            throw err;
        }
    } else if (packet.stream_index == this->gpmf_stream) {
        // TODO process GPMF packet
    } else {
        // TODO pass through audio
    }

    av_packet_unref(&this->packet);
}

AVFrame* AvFrameSourceOpenCl::opencl_frame_from_vaapi_frame(AVFrame *vaapi_frame) {
    int err;
    AVFrame *ocl_frame = av_frame_alloc();
    if (ocl_frame == NULL) {
        cerr << "Failed to allocate OpenCL frame\n";
    }
    AVBufferRef *ocl_hw_frames_ctx = NULL;

    err = av_hwframe_ctx_create_derived(
        &ocl_hw_frames_ctx,
        AV_PIX_FMT_OPENCL,
        this->ocl_device_ctx,
        vaapi_frame->hw_frames_ctx,
        AV_HWFRAME_MAP_DIRECT
    );
    if (err) {
        cerr << "Failed to map hwframes context to OpenCL:" << errString(err) << "\n";
        throw err;
    }

    ocl_frame->hw_frames_ctx = av_buffer_ref(ocl_hw_frames_ctx);
    if (ocl_frame->hw_frames_ctx == NULL) {
        err = AVERROR(ENOMEM);
        cerr << "Failed to allocate reference to hw frames context:" << errString(err) << "\n";
        throw err;
    }

    ocl_frame->format = AV_PIX_FMT_OPENCL;

    err = av_hwframe_map(ocl_frame, vaapi_frame, AV_HWFRAME_MAP_READ);
    if (err) {
        cerr << "Failed to map hardware frames:" << errString(err) << "\n";
        throw err;
    }

    return ocl_frame;
}

AVFrame* AvFrameSourceOpenCl::peek_frame() {
    if (this->next_frame != NULL) {
        return this->next_frame;
    }

    int err = 0;
    AVFrame *vaapi_frame = av_frame_alloc();
    do {
        if (this->input_ended) {
            throw EOF;
        }
        err = avcodec_receive_frame(this->decoder_ctx, vaapi_frame);
        if (!err) {
            break;
        } else if (err == AVERROR(EAGAIN)) {
            this->read_input_packet();
        } else {
            cerr << "Failed to decode frame:" << errString(err) << "\n";
            throw err;
        }
    } while (err == AVERROR(EAGAIN));
    
    this->next_frame = this->opencl_frame_from_vaapi_frame(vaapi_frame);
    av_frame_free(&vaapi_frame);
    return this->next_frame;
}

AVFrame* AvFrameSourceOpenCl::pull_frame() {
    AVFrame *frame = this->peek_frame();
    this->next_frame = NULL;
    return frame;
}

bool AvFrameSourceOpenCl::is_supported() {
    if (!has_hwaccel_support(AV_HWDEVICE_TYPE_VAAPI)) {
        return false;
    }

    if (!has_hwaccel_support(AV_HWDEVICE_TYPE_OPENCL)) {
        return false;
    }
    return true;
}