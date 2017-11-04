#include "avpacketqueue.h"

AvPacketQueue::AvPacketQueue()
{
    mutex   = SDL_CreateMutex();
    cond    = SDL_CreateCond();
}

void AvPacketQueue::enqueue(AVPacket *packet)
{
    SDL_LockMutex(mutex);

    queue.enqueue(*packet);

    SDL_CondSignal(cond);
    SDL_UnlockMutex(mutex);
}

void AvPacketQueue::dequeue(AVPacket *packet, bool isBlock)
{
    SDL_LockMutex(mutex);
    while (1) {
        if (!queue.isEmpty()) {
            *packet = queue.dequeue();
            break;
        } else if (!isBlock) {
            break;
        } else {
            SDL_CondWait(cond, mutex);
        }
    }
    SDL_UnlockMutex(mutex);
}

void AvPacketQueue::empty()
{
    SDL_LockMutex(mutex);
    while (queue.size() > 0) {
        AVPacket packet = queue.dequeue();
        av_packet_unref(&packet);
    }

    SDL_UnlockMutex(mutex);
}

bool AvPacketQueue::isEmpty()
{
    return queue.isEmpty();
}

int AvPacketQueue::queueSize()
{
    return queue.size();
}
