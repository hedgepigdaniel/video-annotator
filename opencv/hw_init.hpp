#ifndef _HW_INIT_H_
#define _HW_INIT_H_

extern "C" {
    #include <libavutil/buffer.h>
    #include <libavformat/avformat.h>
}

bool is_vaapi_and_opencl_supported();

AVBufferRef* create_vaapi_context();

AVBufferRef* create_opencl_context_from_vaapi(AVBufferRef *vaapi_device_ctx);

void init_opencv_from_opencl_context(AVBufferRef *ocl_device_ctx);

#endif // _HW_INIT_H_
