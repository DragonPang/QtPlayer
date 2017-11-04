#ifndef AVPACKETQUEUE_H
#define AVPACKETQUEUE_H

#include <QQueue>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libswresample/swresample.h"
#include "libavdevice/avdevice.h"
#include "libavutil/pixfmt.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
#include "libavcodec/avfft.h"
#include "libavutil/imgutils.h"
}

#include "SDL2/SDL.h"

class AvPacketQueue
{
public:
    explicit AvPacketQueue();

    void enqueue(AVPacket *packet);

    void dequeue(AVPacket *packet, bool isBlock);

    bool isEmpty();

    void empty();

    int queueSize();

private:
    SDL_mutex *mutex;
    SDL_cond *cond;

    QQueue<AVPacket> queue;
};

#endif // AVPACKETQUEUE_H
