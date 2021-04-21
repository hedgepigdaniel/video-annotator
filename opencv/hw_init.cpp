#include "hw_init.hpp"

#include <iostream>
#include <opencv2/core/ocl.hpp>
extern "C" {
    #include <libavutil/hwcontext.h>
    #include <libavutil/hwcontext_opencl.h>
}

#include "utils.hpp"

using namespace std;
using namespace cv;

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

bool is_vaapi_and_opencl_supported() {
    if (!has_hwaccel_support(AV_HWDEVICE_TYPE_VAAPI)) {
        return false;
    }

    if (!has_hwaccel_support(AV_HWDEVICE_TYPE_OPENCL)) {
        return false;
    }
    return true;
}

AVBufferRef* create_vaapi_context() {
    AVBufferRef *vaapi_device_ctx = NULL;
    int ret;
    ret = av_hwdevice_ctx_create(
        &vaapi_device_ctx,
        AV_HWDEVICE_TYPE_VAAPI,
        NULL,
        NULL,
        0
    );
    if (ret < 0) {
        cerr << "Failed to create a VAAPI device context:"<< errString(ret) << "\n";
        throw ret;
    }
    return vaapi_device_ctx;
}

AVBufferRef* create_opencl_context_from_vaapi(AVBufferRef *vaapi_device_ctx) {
    AVBufferRef *opencl_device_ctx = NULL;
    int ret;
    ret = av_hwdevice_ctx_create(
        &opencl_device_ctx,
        AV_HWDEVICE_TYPE_OPENCL,
        "1.0",
        NULL,
        0
    );
    if (ret < 0 || opencl_device_ctx == NULL) {
        cerr << "Failed to map VAAPI device to OpenCL device:" << errString(ret) << "\n";
        throw ret;
    }
    return opencl_device_ctx;
}

void init_opencv_from_opencl_context(AVBufferRef *ocl_device_ctx) {
    int err = 0;
    AVHWDeviceContext *ocl_hw_device_ctx;
    AVOpenCLDeviceContext *ocl_device_ocl_ctx;
    ocl_hw_device_ctx = (AVHWDeviceContext *) ocl_device_ctx->data;
    ocl_device_ocl_ctx = (AVOpenCLDeviceContext *) ocl_hw_device_ctx->hwctx;
    vector<cl_context_properties> props;
    cl_platform_id platform = NULL;
    vector<char> platform_name;
    size_t param_value_size = 0;
    err = clGetContextInfo(
        ocl_device_ocl_ctx->context,
        CL_CONTEXT_PROPERTIES,
        0,
        NULL,
        &param_value_size
    );
    if (err != CL_SUCCESS) {
        std::cerr << "clGetContextInfo failed to get props size\n";
        throw err;
    }
    if (param_value_size == 0) {
        std::cerr << "clGetContextInfo returned size 0\n";
        throw 1;
    }
    props.resize(param_value_size);
    err = clGetContextInfo(
        ocl_device_ocl_ctx->context,
        CL_CONTEXT_PROPERTIES,
        param_value_size,
        props.data(),
        NULL
    );
    if (err != CL_SUCCESS) {
        std::cerr << "clGetContextInfo failed\n";
        throw err;
    }
    for (int i = 0; props[i] != 0; i = i + 2) {
        if (props[i] == CL_CONTEXT_PLATFORM) {
            platform = (cl_platform_id) props[i + 1];
        }
    }
    if (platform == NULL) {
        std::cerr << "Failed to find platform in cl context props\n";
        throw 1;
    }

    err = clGetPlatformInfo(
        platform,
        CL_PLATFORM_NAME,
        0,
        NULL,
        &param_value_size
    );

    if (err != CL_SUCCESS) {
        std::cerr << "clGetPlatformInfo failed to get platform name size\n";
        throw err;
    }
    if (param_value_size == 0) {
        std::cerr << "clGetPlatformInfo returned 0 size for name\n";
        throw 1;
    }
    platform_name.resize(param_value_size);
    err = clGetPlatformInfo(
        platform,
        CL_PLATFORM_NAME,
        param_value_size,
        platform_name.data(),
        NULL
    );
    if (err != CL_SUCCESS) {
        std::cerr << "clGetPlatformInfo failed\n";
        throw err;
    }

    std::cerr << "Initialising OpenCV OpenCL context with platform \"" <<
        string(platform_name.begin(), platform_name.end()) <<
        "\"\n";

    ocl::OpenCLExecutionContext context = ocl::OpenCLExecutionContext::create(
        platform_name.data(),
        platform,
        ocl_device_ocl_ctx->context,
        ocl_device_ocl_ctx->device_id
    );
    context.bind();
}
