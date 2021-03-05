#include "hw_init.hpp"

#include <iostream>
#include <opencv2/core/ocl.hpp>
extern "C" {
    #include <libavutil/hwcontext.h>
    #include <libavutil/hwcontext_opencl.h>
}

using namespace cv;

int init_opencv_opencl_from_hwctx(AVBufferRef *ocl_device_ctx) {
    int ret = 0;
    AVHWDeviceContext *ocl_hw_device_ctx;
    AVOpenCLDeviceContext *ocl_device_ocl_ctx;
    ocl_hw_device_ctx = (AVHWDeviceContext *) ocl_device_ctx->data;
    ocl_device_ocl_ctx = (AVOpenCLDeviceContext *) ocl_hw_device_ctx->hwctx;
    cl_context_properties *props = NULL;
    cl_platform_id platform = NULL;
    char *platform_name = NULL;
    size_t param_value_size = 0;
    ret = clGetContextInfo(
        ocl_device_ocl_ctx->context,
        CL_CONTEXT_PROPERTIES,
        0,
        NULL,
        &param_value_size
    );
    if (ret != CL_SUCCESS) {
        std::cerr << "clGetContextInfo failed to get props size\n";
        return ret;
    }
    if (param_value_size == 0) {
        std::cerr << "clGetContextInfo returned size 0\n";
        return 1;
    }
    props = (cl_context_properties *) malloc(param_value_size);
    if (props == NULL) {
        std::cerr << "Failed to alloc props 0\n";
        return AVERROR(ENOMEM);
    }
    ret = clGetContextInfo(
        ocl_device_ocl_ctx->context,
        CL_CONTEXT_PROPERTIES,
        param_value_size,
        props,
        NULL
    );
    if (ret != CL_SUCCESS) {
        std::cerr << "clGetContextInfo failed\n";
        return ret;
    }
    for (int i = 0; props[i] != 0; i = i + 2) {
        if (props[i] == CL_CONTEXT_PLATFORM) {
            platform = (cl_platform_id) props[i + 1];
        }
    }
    if (platform == NULL) {
        std::cerr << "Failed to find platform in cl context props\n";
        return 1;
    }

    ret = clGetPlatformInfo(
        platform,
        CL_PLATFORM_NAME,
        0,
        NULL,
        &param_value_size
    );

    if (ret != CL_SUCCESS) {
        std::cerr << "clGetPlatformInfo failed to get platform name size\n";
        return ret;
    }
    if (param_value_size == 0) {
        std::cerr << "clGetPlatformInfo returned 0 size for name\n";
        return 1;
    }
    platform_name = (char *) malloc(param_value_size);
    if (platform_name == NULL) {
        std::cerr << "Failed to malloc platform_name\n";
        return AVERROR(ENOMEM);
    }
    ret = clGetPlatformInfo(
        platform,
        CL_PLATFORM_NAME,
        param_value_size,
        platform_name,
        NULL
    );
    if (ret != CL_SUCCESS) {
        std::cerr << "clGetPlatformInfo failed\n";
        return ret;
    }

    std::cerr << "Initialising OpenCV OpenCL context with platform \"" <<
        platform_name <<
        "\"\n";

    ocl::Context::getDefault(false);
    ocl::attachContext(
        platform_name,
        platform,
        ocl_device_ocl_ctx->context,
        ocl_device_ocl_ctx->device_id
    );
    return 0;
}
