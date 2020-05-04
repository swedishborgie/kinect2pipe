#include <iostream>
#include <libfreenect2/logger.h>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include "kinect2pipe.h"
extern "C" {
    #include <libswscale/swscale.h>
    #include <sys/inotify.h>
}
using namespace std;
using namespace libfreenect2;

Kinect2Pipe::Kinect2Pipe () {
    this->sws = sws_getContext(KINECT2_IMAGE_WIDTH, KINECT2_IMAGE_HEIGHT, AV_PIX_FMT_RGB32, KINECT2_IMAGE_WIDTH, KINECT2_IMAGE_HEIGHT, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    memset(this->srcPtr, 0, sizeof(uint8_t*) * 4);
    this->srcStride[0] = KINECT2_IMAGE_WIDTH*4;
    this->srcStride[1] = 0;
    this->srcStride[2] = 0;
    this->srcStride[3] = 0;

    this->imageBuffer = (uint8_t*)calloc(1, YUV_BUFFER_LEN);
    this->dstPtr[0] = this->imageBuffer;
    this->dstPtr[1] = this->imageBuffer + YUV_BUFFER_Y_LEN;
    this->dstPtr[2] = this->imageBuffer + YUV_BUFFER_Y_LEN + YUV_BUFFER_UV_LEN;
    this->dstPtr[3] = nullptr;
    this->dstStride[0] = KINECT2_IMAGE_WIDTH;
    this->dstStride[1] = KINECT2_IMAGE_WIDTH/2;
    this->dstStride[2] = KINECT2_IMAGE_WIDTH/2;
    this->dstStride[3] = 0;

    this->v4l2Device = 0;
    this->inotifyFd = 0;
    this->watcherFd = 0;
    this->openCount = 0;
    this->started = false;

    libfreenect2::setGlobalLogger(nullptr);
}

void Kinect2Pipe::openLoopback(const char *loopbackDev) {
    if (!this->openV4L2LoopbackDevice(loopbackDev, KINECT2_IMAGE_WIDTH, KINECT2_IMAGE_HEIGHT)) {
        exit(1);
    }
    this->writeBlankFrame();
    if (!this->openWatchV4L2LoopbackDevice(loopbackDev)) {
        exit(1);
    }
}

bool Kinect2Pipe::openV4L2LoopbackDevice(const char* loopbackDev, int width, int height) {
    this->v4l2Device = open(loopbackDev, O_WRONLY);
    if (this->v4l2Device < 0) {
        return false;
    }
    struct v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    fmt.fmt.pix.sizeimage = fmt.fmt.pix.width * fmt.fmt.pix.height * 1.5;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = fmt.fmt.pix.width;
    fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    return ioctl(this->v4l2Device, VIDIOC_S_FMT, &fmt) >= 0;
}

bool Kinect2Pipe::openKinect2Device() {
    SyncMultiFrameListener listener(Frame::Color);
    Freenect2Device *dev;
    if (freenect2.enumerateDevices() == 0) {
        std::cerr << "unable to find a kinect2 device to connect to" << endl;
        return false;
    }
    dev = freenect2.openDefaultDevice();
    dev->setColorFrameListener(&listener);
    if (!dev->startStreams(true, false)) {
        std::cerr << "unable to start kinect2 rgb stream" << endl;
        return false;
    }
    while (this->started) {
        if (!listener.waitForNewFrame(frames, 2000)) {
            cerr << "timeout waiting for frame" << endl;
            return false;
        }
        if (!this->handleFrame(frames[Frame::Color])) {
            return false;
        }
        listener.release(frames);
    }
    this->writeBlankFrame();
    dev->stop();
    dev->close();
    return true;
}

bool Kinect2Pipe::handleFrame(Frame* frame) {
    this->srcPtr[0] = frame->data;
    sws_scale(this->sws, this->srcPtr, this->srcStride, 0, KINECT2_IMAGE_HEIGHT, this->dstPtr, this->dstStride);
    return write(this->v4l2Device, this->imageBuffer, YUV_BUFFER_LEN) > 0;
}

bool Kinect2Pipe::openWatchV4L2LoopbackDevice(const char* loopbackDev) {
    this->inotifyFd = inotify_init();
    if (this->inotifyFd < 0) {
        return false;
    }
    this->watcherFd = inotify_add_watch(this->inotifyFd, loopbackDev, IN_OPEN|IN_CLOSE);
    if (this->watcherFd < 0) {
        return false;
    }
    this->watcherThread = thread (&Kinect2Pipe::watchV4L2LoopbackDevice, this);
    this->watcherThread.detach();
    return true;
}

void Kinect2Pipe::watchV4L2LoopbackDevice() {
    // We'll own the lock on this until we're ready to rumble.
    unique_lock<mutex> lk(this->startMutex);
    this->startCondition.wait(lk, [this]{ return !this->started; });
    this->drainInotifyEvents();
    inotify_event events[20];

    fd_set set;
    struct timeval timeout{10, 0};
    while (true) {
        FD_ZERO(&set);
        FD_SET(this->inotifyFd, &set);
        timeout.tv_sec = 10;
        int retr = select(this->inotifyFd + 1, &set, nullptr, nullptr, &timeout);
        if (retr < 0) {
            // error
            cout << "watcher select encountered an problem and quit" << endl;
            return;
        } else if (retr == 0) {
            if (this->openCount == 0 && this->started) {
                cout << "closing device since no one is watching" << endl;
                this->started = false;
                lk.lock();
            }
        } else {
            int len = read(this->inotifyFd, &events, sizeof(events));
            if (len <= 0) {
                cout << "watcher encountered an problem and quit" << endl;
                return;
            }
            for (int i = 0; i < (len / sizeof(inotify_event)); i++) {
                inotify_event *evt = &events[i];
                if (evt->mask & IN_OPEN) {
                    this->openCount++;
                    if (!this->started) {
                        this->started = true;
                        lk.unlock();
                        this->startCondition.notify_one();
                    }

                } else if (evt->mask & IN_CLOSE) {
                    this->openCount--;
                    if (this->openCount <= 0) {
                        this->openCount = 0;
                    }
                }
            }
        }
    }
}

void Kinect2Pipe::drainInotifyEvents() {
    fd_set set;
    struct timeval timeout{2, 0};
    inotify_event events[20];
    FD_ZERO(&set);
    FD_SET(this->inotifyFd, &set);
    while(true) {
        int retr = select(this->inotifyFd + 1, &set, nullptr, nullptr, &timeout);
        if (retr == -1) {
            return;
        } else if (retr == 0) {
            return;
        } else {
            read(this->inotifyFd, &events, sizeof(events));
        }
    }
}

void Kinect2Pipe::run() {
    while(true) {
        unique_lock<mutex> lk(this->startMutex);
        this->startCondition.wait(lk, [this]{ return this->started; });
        if (this->started) {
            cout << "device opened, starting capture" << endl;
            if (!this->openKinect2Device()) {
                exit(-1);
            }
            // If we get here we've shut down.
            lk.unlock();

            //TODO: Fix this, there's a memory leak somewhere in libfreenect2 and we should be able to avoid it and not
            //have to bounce the process every time, but for now this is the cleaner way to do it and let systemd
            //restart us.
            exit(0);
        }
    }
}

void Kinect2Pipe::writeBlankFrame() {
    memset(this->imageBuffer, 0x10, YUV_BUFFER_Y_LEN);
    memset(this->imageBuffer + YUV_BUFFER_Y_LEN, 0, YUV_BUFFER_UV_LEN * 2);
    write(this->v4l2Device, this->imageBuffer, YUV_BUFFER_LEN);
}
