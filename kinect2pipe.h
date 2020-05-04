#ifndef KINECT2PIPE_KINECT2PIPE_H
#define KINECT2PIPE_KINECT2PIPE_H
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace std;
using namespace libfreenect2;

// The base width and height for the Kinect 2 camera.
#define KINECT2_IMAGE_WIDTH 1920
#define KINECT2_IMAGE_HEIGHT 1080

// The YUV420 buffer layout that V4L2 expects -- it expects Y first, followed by U, then V.
// The Y length in the YUV420 buffer is the width times the height of the image.
#define YUV_BUFFER_Y_LEN (KINECT2_IMAGE_WIDTH * KINECT2_IMAGE_HEIGHT)
// The U and V length in the YUV420 buffer is half the width time half the height of the image.
#define YUV_BUFFER_UV_LEN ((KINECT2_IMAGE_WIDTH/2) * (KINECT2_IMAGE_HEIGHT/2))
// Total length of the YUV420 image.
#define YUV_BUFFER_LEN (YUV_BUFFER_Y_LEN + (YUV_BUFFER_UV_LEN * 2))

class Kinect2Pipe {
public:
    explicit Kinect2Pipe();
    void openLoopback(const char* loopbackDev);
    void run();

private:
    Freenect2 freenect2;
    FrameMap frames;

    int v4l2Device;
    struct SwsContext* sws;
    uint8_t* srcPtr[4]{};
    int srcStride[4]{};
    uint8_t* dstPtr[4]{};
    int dstStride[4]{};
    uint8_t* imageBuffer;

    std::thread watcherThread;
    int inotifyFd;
    int watcherFd;
    int openCount;

    mutex startMutex;
    condition_variable startCondition;
    bool started;

    bool openV4L2LoopbackDevice(const char* loopbackDev, int width, int height);
    bool openWatchV4L2LoopbackDevice(const char* lookbackDev);
    bool openKinect2Device();
    bool handleFrame(Frame* frame);
    void watchV4L2LoopbackDevice();
    void drainInotifyEvents();
    void writeBlankFrame();
};


#endif //KINECT2PIPE_KINECT2PIPE_H
