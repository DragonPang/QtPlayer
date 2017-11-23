#include <QDebug>

#include "audiodecoder.h"

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

AudioDecoder::AudioDecoder(QObject *parent) :
    QObject(parent),
    isStop(false),
    isPause(false),
    isreadFinished(false),
    totalTime(0),
    clock(0),
    volume(SDL_MIX_MAXVOLUME),
    audioDeviceFormat(AUDIO_F32SYS),
    aCovertCtx(NULL),
    sendReturn(0)
{

}

int AudioDecoder::openAudio(AVFormatContext *pFormatCtx, int index)
{
    AVCodec *codec;
    SDL_AudioSpec wantedSpec;
    int wantedNbChannels;
    const char *env;

    /*  soundtrack array use to adjust */
    int nextNbChannels[]   = {0, 0, 1, 6, 2, 6, 4, 6};
    int nextSampleRates[]  = {0, 44100, 48000, 96000, 192000};
    int nextSampleRateIdx = FF_ARRAY_ELEMS(nextSampleRates) - 1;

    isStop = false;
    isPause = false;
    isreadFinished = false;

    audioSrcFmt = AV_SAMPLE_FMT_NONE;
    audioSrcChannelLayout = 0;
    audioSrcFreq = 0;

    pFormatCtx->streams[index]->discard = AVDISCARD_DEFAULT;

    stream = pFormatCtx->streams[index];

    codecCtx = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[index]->codecpar);

    /* find audio decoder */
    if ((codec = avcodec_find_decoder(codecCtx->codec_id)) == NULL) {
        avcodec_free_context(&codecCtx);
        qDebug() << "Audio decoder not found.";
        return -1;
    }

    /* open audio decoder */
    if (avcodec_open2(codecCtx, codec, NULL) < 0) {
        avcodec_free_context(&codecCtx);
        qDebug() << "Could not open audio decoder.";
        return -1;
    }

    totalTime = pFormatCtx->duration;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        qDebug() << "SDL audio channels";
        wantedNbChannels = atoi(env);
        audioDstChannelLayout = av_get_default_channel_layout(wantedNbChannels);
    }

    wantedNbChannels = codecCtx->channels;
    if (!audioDstChannelLayout ||
        (wantedNbChannels != av_get_channel_layout_nb_channels(audioDstChannelLayout))) {
        audioDstChannelLayout = av_get_default_channel_layout(wantedNbChannels);
        audioDstChannelLayout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }

    wantedSpec.channels    = av_get_channel_layout_nb_channels(audioDstChannelLayout);
    wantedSpec.freq        = codecCtx->sample_rate;
    if (wantedSpec.freq <= 0 || wantedSpec.channels <= 0) {
        avcodec_free_context(&codecCtx);
        qDebug() << "Invalid sample rate or channel count, freq: " << wantedSpec.freq << " channels: " << wantedSpec.channels;
        return -1;
    }

    while (nextSampleRateIdx && nextSampleRates[nextSampleRateIdx] >= wantedSpec.freq) {
        nextSampleRateIdx--;
    }

    wantedSpec.format      = audioDeviceFormat;
    wantedSpec.silence     = 0;
    wantedSpec.samples     = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wantedSpec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wantedSpec.callback    = &AudioDecoder::audioCallback;
    wantedSpec.userdata    = this;

    /* This function opens the audio device with the desired parameters, placing
     * the actual hardware parameters in the structure pointed to spec.
     */
    while (1) {
        while (SDL_OpenAudio(&wantedSpec, &spec) < 0) {
            qDebug() << QString("SDL_OpenAudio (%1 channels, %2 Hz): %3")
                    .arg(wantedSpec.channels).arg(wantedSpec.freq).arg(SDL_GetError());
            wantedSpec.channels = nextNbChannels[FFMIN(7, wantedSpec.channels)];
            if (!wantedSpec.channels) {
                wantedSpec.freq = nextSampleRates[nextSampleRateIdx--];
                wantedSpec.channels = wantedNbChannels;
                if (!wantedSpec.freq) {
                    avcodec_free_context(&codecCtx);
                    qDebug() << "No more combinations to try, audio open failed";
                    return -1;
                }
            }
            audioDstChannelLayout = av_get_default_channel_layout(wantedSpec.channels);
        }

        if (spec.format != audioDeviceFormat) {
            qDebug() << "SDL audio format: " << wantedSpec.format << " is not supported"
                     << ", set to advised audio format: " <<  spec.format;
            wantedSpec.format = spec.format;
            audioDeviceFormat = spec.format;
            SDL_CloseAudio();
        } else {
            break;
        }
    }

    if (spec.channels != wantedSpec.channels) {
        audioDstChannelLayout = av_get_default_channel_layout(spec.channels);
        if (!audioDstChannelLayout) {
            avcodec_free_context(&codecCtx);
            qDebug() << "SDL advised channel count " << spec.channels << " is not supported!";
            return -1;
        }
    }

    /* set sample format */
    switch (audioDeviceFormat) {
    case AUDIO_U8:
        audioDstFmt    = AV_SAMPLE_FMT_U8;
        audioDepth = 1;
        break;

    case AUDIO_S16SYS:
        audioDstFmt    = AV_SAMPLE_FMT_S16;
        audioDepth = 2;
        break;

    case AUDIO_S32SYS:
        audioDstFmt    = AV_SAMPLE_FMT_S32;
        audioDepth = 4;
        break;

    case AUDIO_F32SYS:
        audioDstFmt    = AV_SAMPLE_FMT_FLT;
        audioDepth = 4;
        break;

    default:
        audioDstFmt    = AV_SAMPLE_FMT_S16;
        audioDepth = 2;
        break;
    }

    /* open sound */
    SDL_PauseAudio(0);

    return 0;
}

void AudioDecoder::closeAudio()
{
    emptyAudioData();

    SDL_LockAudio();
    SDL_CloseAudio();
    SDL_UnlockAudio();

    avcodec_close(codecCtx);
    avcodec_free_context(&codecCtx);
}

void AudioDecoder::readFileFinished()
{
    isreadFinished = true;
}

void AudioDecoder::pauseAudio(bool pause)
{
    isPause = pause;
}

void AudioDecoder::stopAudio()
{
    isStop = true;
}

void AudioDecoder::packetEnqueue(AVPacket *packet)
{
    packetQueue.enqueue(packet);
}

void AudioDecoder::emptyAudioData()
{
    audioBuf = nullptr;

    audioBufIndex = 0;
    audioBufSize = 0;
    audioBufSize1 = 0;

    clock = 0;

    sendReturn = 0;

    packetQueue.empty();
}

int AudioDecoder::getVolume()
{
    return volume;
}

void AudioDecoder::setVolume(int volume)
{
    this->volume = volume;
}

double AudioDecoder::getAudioClock()
{
    if (codecCtx) {
        /* control audio pts according to audio buffer data size */
        int hwBufSize   = audioBufSize - audioBufIndex;
        int bytesPerSec = codecCtx->sample_rate * codecCtx->channels * audioDepth;

        clock -= static_cast<double>(hwBufSize) / bytesPerSec;
    }

    return clock;
}

void AudioDecoder::audioCallback(void *userdata, quint8 *stream, int SDL_AudioBufSize)
{
    AudioDecoder *decoder = (AudioDecoder *)userdata;

    int decodedSize;
    /* SDL_BufSize means audio play buffer left size
     * while it greater than 0, means counld fill data to it
     */
    while (SDL_AudioBufSize > 0) {
        if (decoder->isStop) {
            return ;
        }

        if (decoder->isPause) {
            SDL_Delay(10);
            continue;
        }

        /* no data in buffer */
        if (decoder->audioBufIndex >= decoder->audioBufSize) {

            decodedSize = decoder->decodeAudio();
            /* if error, just output silence */
            if (decodedSize < 0) {
                /* if not decoded data, just output silence */
                decoder->audioBufSize = 1024;
                decoder->audioBuf = nullptr;
            } else {
                decoder->audioBufSize = decodedSize;
            }
            decoder->audioBufIndex = 0;
        }

        /* calculate number of data that haven't play */
        int left = decoder->audioBufSize - decoder->audioBufIndex;
        if (left > SDL_AudioBufSize) {
            left = SDL_AudioBufSize;
        }

        if (decoder->audioBuf) {
            memset(stream, 0, left);
            SDL_MixAudio(stream, decoder->audioBuf + decoder->audioBufIndex, left, decoder->volume);
        }

        SDL_AudioBufSize -= left;
        stream += left;
        decoder->audioBufIndex += left;
    }
}

int AudioDecoder::decodeAudio()
{
    int ret;
    AVFrame *frame = av_frame_alloc();
    int resampledDataSize;

    if (!frame) {
        qDebug() << "Decode audio frame alloc failed.";
        return -1;
    }

    if (isStop) {
        return -1;
    }

    if (packetQueue.queueSize() <= 0) {
        if (isreadFinished) {
            isStop = true;
            SDL_Delay(100);
            emit playFinished();
        }
        return -1;
    }

    /* get new packet whiel last packet all has been resolved */
    if (sendReturn != AVERROR(EAGAIN)) {
        packetQueue.dequeue(&packet, true);
    }

    if (!strcmp((char*)packet.data, "FLUSH")) {
        avcodec_flush_buffers(codecCtx);
        av_packet_unref(&packet);
        av_frame_free(&frame);
        sendReturn = 0;
        qDebug() << "seek audio";
        return -1;
    }

    /* while return -11 means packet have data not resolved,
     * this packet cannot be unref
     */
    sendReturn = avcodec_send_packet(codecCtx, &packet);
    if ((sendReturn < 0) && (sendReturn != AVERROR(EAGAIN)) && (sendReturn != AVERROR_EOF)) {
        av_packet_unref(&packet);
        av_frame_free(&frame);
        qDebug() << "Audio send to decoder failed, error code: " << sendReturn;
        return sendReturn;
    }

    ret = avcodec_receive_frame(codecCtx, frame);
    if ((ret < 0) && (ret != AVERROR(EAGAIN))) {
        av_packet_unref(&packet);
        av_frame_free(&frame);
        qDebug() << "Audio frame decode failed, error code: " << ret;
        return ret;
    }

    if (frame->pts != AV_NOPTS_VALUE) {
        clock = av_q2d(stream->time_base) * frame->pts;
//        qDebug() << "no pts";
    }

    /* get audio channels */
    qint64 inChannelLayout = (frame->channel_layout && frame->channels == av_get_channel_layout_nb_channels(frame->channel_layout)) ?
                frame->channel_layout : av_get_default_channel_layout(frame->channels);

    if (frame->format       != audioSrcFmt              ||
        inChannelLayout     != audioSrcChannelLayout    ||
        frame->sample_rate  != audioSrcFreq             ||
        !aCovertCtx) {
        if (aCovertCtx) {
            swr_free(&aCovertCtx);
        }

        /* init swr audio convert context */
        aCovertCtx = swr_alloc_set_opts(nullptr, audioDstChannelLayout, audioDstFmt, spec.freq,
                inChannelLayout, (AVSampleFormat)frame->format , frame->sample_rate, 0, NULL);
        if (!aCovertCtx || (swr_init(aCovertCtx) < 0)) {
            av_packet_unref(&packet);
            av_frame_free(&frame);
            return -1;
        }

        audioSrcFmt             = (AVSampleFormat)frame->format;
        audioSrcChannelLayout   = inChannelLayout;
        audioSrcFreq            = frame->sample_rate;
        audioSrcChannels        = frame->channels;
    }

    if (aCovertCtx) {
        const quint8 **in   = (const quint8 **)frame->extended_data;
        uint8_t *out[] = {audioBuf1};

        int outCount = sizeof(audioBuf1) / spec.channels / av_get_bytes_per_sample(audioDstFmt);

        int sampleSize = swr_convert(aCovertCtx, out, outCount, in, frame->nb_samples);
        if (sampleSize < 0) {
            qDebug() << "swr convert failed";
            av_packet_unref(&packet);
            av_frame_free(&frame);
            return -1;
        }

        if (sampleSize == outCount) {
            qDebug() << "audio buffer is probably too small";
            if (swr_init(aCovertCtx) < 0) {
                swr_free(&aCovertCtx);
            }
        }

        audioBuf = audioBuf1;
        resampledDataSize = sampleSize * spec.channels * av_get_bytes_per_sample(audioDstFmt);
    } else {
        audioBuf = frame->data[0];
        resampledDataSize = av_samples_get_buffer_size(NULL, frame->channels, frame->nb_samples, static_cast<AVSampleFormat>(frame->format), 1);
    }

    clock += static_cast<double>(resampledDataSize) / (audioDepth * codecCtx->channels * codecCtx->sample_rate);

    if (sendReturn != AVERROR(EAGAIN)) {
        av_packet_unref(&packet);
    }

    av_frame_free(&frame);

    return resampledDataSize;
}
