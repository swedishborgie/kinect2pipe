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
    /**
     * Constructor, initializes the swscale library and gets ready to work.
     */
    explicit Kinect2Pipe();
    /**
     * openLoopback opens the v4l2loopback device writes the video metadata to the ioctl and writes a single blank
     * frame. It then listens for applications opening handles to the device before starting the video stream.
     * @param loopbackDev The path to the v4l2loopback device to open.
     */
    void openLoopback(const char* loopbackDev);
    /**
     * run waits until an application opens the v4l2loopback device and once it does it'll start the video stream.
     */
    void run();

private:
    /**
     * Reference to the libfreenect library.
     */
    Freenect2 freenect2;
    /**
     * The set of frames from the libfreenect2 library from the listener callback.
     */
    FrameMap frames;
    /**
     * File descriptor to the loopback device.
     */
    int v4l2Device;
    /**
     * Context for the swscale frame converter.
     */
    struct SwsContext* sws;
    /**
     * Source pointers for swscale conversion. Each index is associated with a different color plane of the image. Since
     * the source from the Kinect 2 is always an RGB image converted from the JPEG stream from the camera there will
     * always will only include a single plane.
     */
    uint8_t* srcPtr[4]{};
    /**
     * Source stride values for the source image. The libswscale library expects this to be the number of bytes in a
     * single horizontal line. The source image from the Kinect is an RGB32 image with a single color plane so this
     * should always be a single element with a width of: (image width * 4) (r, g, b, a).
     */
    int srcStride[4]{};
    /**
     * Destination pointers for the swscale conversion. Each index is associated with a different color plane in the
     * destination image. Since we're converting to YUV420 we'll have three different color planes (Y, U, V). The
     * v4l2loopback interface expects all three planes to be a contiguous byte array. The Y plane is YUV_BUFFER_Y_LEN
     * long, while the U and V channels are YUV_BUFFER_UV_LEN long. There should be three pointers in this array.
     */
    uint8_t* dstPtr[4]{};
    /**
     * Destination stride values for each color plane in the destination image. This should be the number of bytes in
     * a single horizontal line for the specified color plane (Y, U, V). The Y plane should be 1 byte per pixel, U and
     * V are half resolution.
     */
    int dstStride[4]{};
    /**
     * The destination image buffer to write frames into.
     */
    uint8_t* imageBuffer;
    /**
     * This is the inotify watcher thread listening for events on the v4l2loopback device.
     */
    std::thread watcherThread;
    /**
     * The inotify handle for the v4l2loopback device.
     */
    int inotifyFd;
    /**
     * The watcher handle for the v4l2loopback device.
     */
    int watcherFd;
    /**
     * The number of open file handles to the v4l2loopback device. We'll exit when we hit zero for a period of time.
     */
    int openCount;
    /**
     * Mutex that's used to gate the start condition this mutex will release when a v4l2 client has attached to the
     * v4l2loopback device.
     */
    mutex startMutex;
    /**
     * This start condition will be true when a client has attached to the v4l2loopback device.
     */
    condition_variable startCondition;
    /**
     * Whether or not we've started the video stream.
     */
    bool started;

    /**
     * openV4L2LoopbackDevice opens up the loopback device and writes the ioctl setting the stream parameters.
     * @param loopbackDev Loopback device to open.
     * @param width Width of the output video.
     * @param height Height of the output video.
     * @return True if everything goes well, false otherwise.
     */
    bool openV4L2LoopbackDevice(const char* loopbackDev, int width, int height);
    /**
     * openWatchV4L2LoopbackDevice creates a thread that calls watchV4L2LoopbackDevice that will watch the v4l2loopback
     * device for processes opening and closing the device.
     * @param lookbackDev The loopback device to watch.
     * @return True if everything goes well, false otherwise.
     */
    bool openWatchV4L2LoopbackDevice(const char* lookbackDev);
    /**
     * openKinect2Device will open and read from a Kinect 2 device. This method will block and read from the device
     * until shut down or an error occurs.
     * @return True if exiting normally, false if an error occurred.
     */
    bool openKinect2Device();
    /**
     * handleFrame will handle an RGB frame from the libfreenect2 library.
     * @param frame Frame to convert and write to the v4l2loopback device.
     * @return True if everything went well, false if there was a problem and we should abort.
     */
    bool handleFrame(Frame* frame);
    /**
     * watchV4L2LoopbackDevice method will attach an inotify listener to the requested v4l2loopback device and will
     * start up the Kinect frame listener when the video device gets opened.
     */
    void watchV4L2LoopbackDevice();
    /**
     * drainInotifyEvents will read from the inotify handle started in watchV4L2LoopbackDevice until there have been no
     * events for two seconds. We do this because upon opening the inotify handle we can get old buffered events.
     */
    void drainInotifyEvents();
    /**
     * This method writes a blank frame to the v4l2loopback device.
     */
    void writeBlankFrame();
};

#endif //KINECT2PIPE_KINECT2PIPE_H
