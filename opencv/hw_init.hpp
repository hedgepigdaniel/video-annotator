#ifndef _HW_INIT_H_
#define _HW_INIT_H_

extern "C" {
    #include <libavutil/buffer.h>
}


int init_opencv_opencl_from_hwctx(AVBufferRef *ocl_device_ctx);

#endif // _HW_INIT_H_
