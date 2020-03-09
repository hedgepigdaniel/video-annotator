#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/core/va_intel.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <va/va_drm.h>

#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace cv;
using namespace std;

#define DRM_DEVICE_PATH "/dev/dri/renderD128"

void printOpenClInfo(void) {
    ocl::Context context;
    if (!context.create(ocl::Device::TYPE_ALL))
    {
        cout << "Failed creating the context..." << endl;
        //return;
    }

    cout << context.ndevices() << " CPU devices are detected." << endl; //This bit provides an overview of the OpenCL devices you have in your computer
    for (int i = 0; i < context.ndevices(); i++)
    {
        ocl::Device device = context.device(i);
        cout << "name:              " << device.name() << endl;
        cout << "available:         " << device.available() << endl;
        cout << "imageSupport:      " << device.imageSupport() << endl;
        cout << "OpenCL_C_Version:  " << device.OpenCL_C_Version() << endl;
        cout << endl;
    }
}

int main(int, char**)
{
    int drm_device = -1;
    drm_device = open(DRM_DEVICE_PATH, O_RDWR|O_CLOEXEC);
    std::cout << "mtp:: drm_device= " << drm_device << std::endl;
    if(drm_device < 0)
    {
        std::cout << "mtp:: GPU device not found...Exiting" << std::endl;
        exit(-1);
    }
    VADisplay vaDisplay = vaGetDisplayDRM(drm_device);
    if(!vaDisplay)
       std::cout << "mtp:: Not a valid display" << std::endl;
    close(drm_device);
    if(!vaDisplay)
        std::cout << "mtp:: Not a valid display::2nd" << std::endl;
    va_intel::ocl::initializeContextFromVA (vaDisplay, true);
    printOpenClInfo();

    UMat frame, display;
    //--- INITIALIZE VIDEOCAPTURE
    VideoCapture cap(
        "filesrc location=/home/daniel/dodgeball/7in/2020-03-02/1420.mkv ! demux ! vaapih264dec ! appsink sync=false",
        CAP_GSTREAMER
    );
    cap.setExceptionMode(true);
    // check if we succeeded
    if (!cap.isOpened()) {
        cerr << "ERROR! Unable to open camera\n";
        return -1;
    }
    //--- GRAB AND WRITE LOOP
    cout << "Using backend " << cap.getBackendName() << "\n";
    cout << "Start grabbing" << endl
        << "Press any key to terminate" << endl;
    for (int i = 0;;i++)
    {
        cout << "frame " << i << "\n";
        // wait for a new frame from camera and store it into 'frame'
        cap.read(frame);
        // check if we succeeded
        if (frame.empty()) {
            cerr << "ERROR! blank frame grabbed\n";
            break;
        }
        if (i % 60 == 0) {
            cvtColor(frame, display, COLOR_YUV2BGR_NV12);
            // show live and wait for a key with timeout long enough to show images
            imshow("Live", display);
            cerr << "Channels: " << display.channels() << "\n";
            cerr << "Type: " << display.type() << "\n";
            cerr << "CV_8U: " << CV_8U << "\n";
            cerr << "Size: " << display.size << "\n";
            cerr << "Step: " << display.step << "\n";
            // cerr << "Data: " << display.data << "\n";
            cerr << "U: " << display.u << "\n";
            if (waitKey(5) >= 0)
                break;

        }
    }
    // the camera will be deinitialized automatically in VideoCapture destructor
    return 0;
}