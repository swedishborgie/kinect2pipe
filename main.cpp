#include "kinect2pipe.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: kinect2pipe [path to v4l2loopback device]\n");
        exit(-1);
    }

    auto* pipe = new Kinect2Pipe();
    pipe->openLoopback(argv[1]);
    pipe->run();
}