#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <QObject>

extern "C"
{
#include "libswresample/swresample.h"
}

#include "avpacketqueue.h"

class AudioDecoder : public QObject
{
    Q_OBJECT
public:
    explicit AudioDecoder(QObject *parent = nullptr);

    int openAudio(AVFormatContext *pFormatCtx, int index);
    void closeAudio();
    void pauseAudio(bool pause);
    void stopAudio();
    int getVolume();
    void setVolume(int volume);
    double getAudioClock();
    void packetEnqueue(AVPacket *packet);
    void emptyAudioData();
    void setTotalTime(qint64 time);

private:
    int decodeAudio();
    static void audioCallback(void *userdata, quint8 *stream, int SDL_AudioBufSize);

    bool isStop;
    bool isPause;
    bool isreadFinished;

    qint64 totalTime;
    double clock;
    int volume;

    AVStream *stream;

    quint8 *audioBuf;
    quint32 audioBufSize;
    DECLARE_ALIGNED(16, quint8, audioBuf1) [192000];
    quint32 audioBufSize1;
    quint32 audioBufIndex;

    SDL_AudioSpec spec;

    quint32 audioDeviceFormat;  // audio device sample format
    quint8 audioDepth;
    struct SwrContext *aCovertCtx;
    qint64 audioDstChannelLayout;
    enum AVSampleFormat audioDstFmt;   // audio decode sample format

    qint64 audioSrcChannelLayout;
    int audioSrcChannels;
    enum AVSampleFormat audioSrcFmt;
    int audioSrcFreq;

    AVCodecContext *codecCtx;          // audio codec context

    AvPacketQueue packetQueue;

    AVPacket packet;

    int sendReturn;

signals:
    void playFinished();

public slots:
    void readFileFinished();

};

#endif // AUDIODECODER_H
