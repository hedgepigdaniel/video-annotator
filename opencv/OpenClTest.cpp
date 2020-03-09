#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <iostream>
using namespace cv;
using namespace std;


string imagePath="/usr/share/opencv4/samples/data/lena.jpg";

int TestOpenCL(int nbTest);


int main(int argc, char **argv)
{
    if (!cv::ocl::haveOpenCL())
    {
        cout << "OpenCL is not avaiable..." << endl;
        return 0;
    }
    cv::ocl::Context context;
    std::vector<cv::ocl::PlatformInfo> platforms;
    cv::ocl::getPlatfomsInfo(platforms);

    //OpenCL Platforms
    for (size_t i = 0; i < platforms.size(); i++)
    {
        //Access to Platform
        const cv::ocl::PlatformInfo* platform = &platforms[i];

        //Platform Name
        std::cout << "Platform Name: " << platform->name().c_str() << "\n";

        //Access Device within Platform
        cv::ocl::Device current_device;
        for (int j = 0; j < platform->deviceNumber(); j++)
        {
            //Access Device
            platform->getDevice(current_device, j);
            
            //Device Type
            int deviceType = current_device.type();
            switch (deviceType) {
            case (1 << 1):
                cout<<" CPU device\n";
                break;
            case (1 << 2) :
                cout << " GPU device\n";
                
                if (context.create(deviceType))
                    TestOpenCL(50);
                break;

            }
         cout << context.ndevices() << " GPU devices are detected." << std::endl; 
           
            cout<< deviceType <<"\n";
        }
    }
    return 0;
}


int TestOpenCL(int nbTest)
{

    cout << "Without opencl umat \t with opencl umat\t with opencl mat \t without opencl mat for cvtColor(0),Blur(1),Canny(2)\n";
    cout << "getNumberOfCPUs =" << getNumberOfCPUs() << "\t getNumThreads = " << getNumThreads() << "\n";
    Mat image = imread(imagePath.c_str(), IMREAD_UNCHANGED);
    double totalTime = 0;
    UMat uimage;
    imread(imagePath.c_str(), IMREAD_UNCHANGED).copyTo(uimage);
    vector<double> tps;
    
    TickMeter myChrono;
    double m,v;
    // ********************** CVTCOLOR ************
    {
        // 1 UMAT ocl false
        ocl::setUseOpenCL(false);
        cout << "cvtColor = ";
        tps.clear();
        tps.resize(nbTest);
        UMat ugray;
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            cvtColor(uimage, ugray, COLOR_BGR2GRAY);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m=0;v=0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m+= tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m-tps[nTest],2.0);
        v /= nbTest;
        cout<<"("<<m<<" +/-"<<sqrt(v)<<")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t" ;
        else
            cout << "true\t\t";

    }

    {
        // 2 UMAT ocl true
        ocl::setUseOpenCL(true);
        tps.clear();
        tps.resize(nbTest);
        UMat ugray;
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            cvtColor(uimage, ugray, COLOR_BGR2GRAY);
            myChrono.stop();
            tps[nTest]=myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
    }

    // 3 MAT ocl false
    {
        ocl::setUseOpenCL(false);
        tps.clear();
        tps.resize(nbTest);
        Mat gray;
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            cvtColor(image, gray, COLOR_BGR2GRAY);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";

    }
    {
        // 4 MAT ocl true
        ocl::setUseOpenCL(true);
        tps.clear();
        tps.resize(nbTest);
        Mat gray;
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            cvtColor(image, gray, COLOR_BGR2GRAY);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << "";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
        cout<<"\n";
    }
    Size tailleBlur(101, 101);

    // ********************** GAUSSIANBLUR ************
    {
        // 1 UMAT ocl false
        cout << "gaussianblur = ";
        ocl::setUseOpenCL(false);
        tps.clear();
        tps.resize(nbTest);
        UMat ugray, ugrayres;
        cvtColor(uimage, ugray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            GaussianBlur(ugray, ugrayres, tailleBlur, 1.5);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";    
    }
    {
        // 2 UMAT ocl true
        ocl::setUseOpenCL(true);
        tps.clear();
        tps.resize(nbTest);
        UMat ugray, ugrayres;
        cvtColor(uimage, ugray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            GaussianBlur(ugray, ugrayres, tailleBlur, 1.5);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
    }
    {
        // 3 MAT ocl false
        ocl::setUseOpenCL(false);
        tps.clear();
        tps.resize(nbTest);
        Mat gray,grayres;
        cvtColor(image, gray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            GaussianBlur(gray, grayres, tailleBlur, 1.5);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
    }
    {
        // 4 MAT ocl true
        ocl::setUseOpenCL(true);
        tps.clear();
        tps.resize(nbTest);
        Mat gray, grayres;
        cvtColor(image, gray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            GaussianBlur(gray, grayres, tailleBlur, 1.5);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
        cout<< "\n";
    }
    // ********************** CANNY ************
    // 1 UMAT ocl false
    cout << "Canny = ";
    {
        ocl::setUseOpenCL(false);
        tps.clear();
        tps.resize(nbTest);
        UMat ugray, ugrayres;
        cvtColor(uimage, ugray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            Canny(ugray, ugrayres, 0, 50);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
    }
    // 2 UMAT ocl true
    {
        ocl::setUseOpenCL(true);
        tps.clear();
        tps.resize(nbTest);
        UMat ugray,ugrayres;
        cvtColor(uimage, ugray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            Canny(ugray, ugrayres, 0, 50);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
    }
    // 3 MAT ocl false
    {
        ocl::setUseOpenCL(false);
        tps.clear();
        tps.resize(nbTest);
        Mat gray,grayres;
        cvtColor(image, gray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            Canny(gray, grayres, 0, 50);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
    }
    // 4 MAT ocl true
    {
        ocl::setUseOpenCL(true);
        tps.clear();
        tps.resize(nbTest);
        Mat gray,grayres;
        cvtColor(image, gray, COLOR_BGR2GRAY);
        for (int nTest = 0; nTest<nbTest; nTest++)
        {
            myChrono.reset();
            myChrono.start();
            Canny(gray, grayres, 0, 50);
            myChrono.stop();
            tps[nTest] = myChrono.getTimeMilli();
        }
        m = 0; v = 0;
        for (int nTest = 0; nTest<nbTest; nTest++)
            m += tps[nTest];
        m /= nbTest;
        for (int nTest = 0; nTest<nbTest; nTest++)
            v += pow(m - tps[nTest], 2.0);
        v /= nbTest;
        cout << "(" << m << " +/-" << sqrt(v) << ")";
        if (!cv::ocl::useOpenCL())
            cout << "false\t\t";
        else
            cout << "true\t\t";
        cout << "\n";

    }
    return 0;
}
