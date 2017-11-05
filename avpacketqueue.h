#ifndef AVPACKETQUEUE_H
#define AVPACKETQUEUE_H

#include <QQueue>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
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
