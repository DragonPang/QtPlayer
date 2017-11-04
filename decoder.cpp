#include <QDebug>

#include "decoder.h"

Decoder::Decoder() :
    playState(STOP),
    isStop(false),
    isPause(false),
    isSeek(false),
    isReadFinished(false),
    audioDecoder(new AudioDecoder)
{
    av_init_packet(&seekPacket);
    seekPacket.data = (uint8_t *)"FLUSH";

    connect(audioDecoder, SIGNAL(playFinished()), this, SLOT(audioFinished()));
    connect(this, SIGNAL(readFinished()), audioDecoder, SLOT(readFileFinished()));
}

Decoder::~Decoder()
{

}

void Decoder::displayVideo(QImage image)
{
    emit gotVideo(image);
}

void Decoder::clearData()
{
    isStop  = false;
    isPause = false;
    isSeek  = false;
    isReadFinished      = false;
    isDecodeFinished    = false;

    videoQueue.empty();

    videoClk = 0;
}

void Decoder::setPlayState(Decoder::PlayState state)
{
//    qDebug() << "Set state: " << state;
    emit playStateChanged(state);
    playState = state;
}

bool Decoder::isRealtime(AVFormatContext *pFormatCtx)
{
    if (!strcmp(pFormatCtx->iformat->name, "rtp")
        || !strcmp(pFormatCtx->iformat->name, "rtsp")
        || !strcmp(pFormatCtx->iformat->name, "sdp")) {
         return true;
    }

    if(pFormatCtx->pb && (!strncmp(pFormatCtx->filename, "rtp:", 4)
        || !strncmp(pFormatCtx->filename, "udp:", 4)
        )) {
        return true;
    }

    return false;
}

void Decoder::decoderFile(QString file, QString type)
{
//    qDebug() << "Current state:" << playState;
    qDebug() << "File name: " << file << ", type: " << type;
    if (playState != STOP) {
        isStop = true;
        while (playState != STOP) {
            SDL_Delay(10);
        }
        SDL_Delay(100);
    }

    clearData();

    currentFile = file;
    currentType = type;

    this->start();
}

void Decoder::audioFinished()
{
    isStop = true;
    if (currentType == "music") {
        SDL_Delay(100);
        emit playStateChanged(Decoder::FINISH);
    }
}

void Decoder::stopVideo()
{
    if (playState == STOP) {
        return;
    }

    gotStop = true;
    isStop  = true;
    audioDecoder->stopAudio();

    if (currentType == "video") {
        /* wait for decoding & reading stop */
        while (!isReadFinished || !isDecodeFinished) {
            SDL_Delay(10);
        }
    } else {
        while (!isReadFinished) {
            SDL_Delay(10);
        }
    }
}

void Decoder::pauseVideo()
{
    if (playState == STOP) {
        return;
    }

    isPause = !isPause;
    audioDecoder->pauseAudio(isPause);
    if (isPause) {
        setPlayState(PAUSE);
    } else {
        setPlayState(PLAYING);
    }
}

int Decoder::getVolume()
{
    return audioDecoder->getVolume();
}

void Decoder::setVolume(int volume)
{
    audioDecoder->setVolume(volume);
}

double Decoder::getCurrentTime()
{
    return audioDecoder->getAudioClock();
}

void Decoder::seekProgress(qint64 pos)
{
    if (!isSeek) {
        seekPos = pos;
        isSeek = true;
    }
}

double Decoder::synchronize(AVFrame *frame, double pts)
{
    double delay;

    if (pts != 0) {
        videoClk = pts; // Get pts,then set video clock to it
    } else {
        pts = videoClk; // Don't get pts,set it to video clock
    }

    delay = av_q2d(pCodecCtx->time_base);
    delay += frame->repeat_pict * (delay * 0.5);

    videoClk += delay;

    return pts;
}

int Decoder::videoThread(void *arg)
{
    Decoder *decoder = (Decoder *)arg;
    AVFrame *pFrame     = av_frame_alloc();
    AVFrame *pFrameRGB  = av_frame_alloc();
    AVPacket packet;
    double pts;
    int ret;

    quint8 *videoBuf = static_cast<quint8 *>(av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB32, decoder->pCodecCtx->width, decoder->pCodecCtx->height, 1)));

    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize,
                            videoBuf, AV_PIX_FMT_RGB32, decoder->pCodecCtx->width, decoder->pCodecCtx->height, 1);

    while (true) {
        if (decoder->isStop) {
            break;
        }

        if (decoder->isPause) {
            SDL_Delay(10);
            continue;
        }

        if (decoder->videoQueue.queueSize() <= 0) {
            /* while video file read finished exit decode thread,
             * otherwise just delay for data input
             */
            if (decoder->isReadFinished) {
                break;
            }
            SDL_Delay(1);
            continue;
        }

        decoder->videoQueue.dequeue(&packet, true);

        /* flush codec buffer while received flush packet */
        if (!strcmp((char *)packet.data, "FLUSH")) {
            qDebug() << "Seek video";
            avcodec_flush_buffers(decoder->pCodecCtx);
            av_packet_unref(&packet);
            continue;
        }

        ret = avcodec_send_packet(decoder->pCodecCtx, &packet);
        if ((ret < 0) && (ret != AVERROR(EAGAIN)) && (ret != AVERROR_EOF)) {
            qDebug() << "Video send to decoder failed, error code: " << ret;
            av_packet_unref(&packet);
            continue;
        }

        ret = avcodec_receive_frame(decoder->pCodecCtx, pFrame);
        if ((ret < 0) && (ret != AVERROR_EOF)) {
            qDebug() << "Video frame decode failed, error code: " << ret;
            av_packet_unref(&packet);           
            continue;
        }

        if ((pts = av_frame_get_best_effort_timestamp(pFrame)) == AV_NOPTS_VALUE) {
            pts = 0;
        }

        pts *= av_q2d(decoder->videoStream->time_base);
        pts =  decoder->synchronize(pFrame, pts);

        while (1) {
            if (decoder->isStop) {
                break;
            }

            double audioClk = decoder->audioDecoder->getAudioClock();
            pts = decoder->videoClk;

            if (pts <= audioClk) {
                 break;
            }
            int delayTime = (pts - audioClk) * 1000;

            delayTime = delayTime > 5 ? 5 : delayTime;

            SDL_Delay(delayTime);
        }

        /* change avFrame to RGB */
        ret = sws_scale(decoder->imgCovertCtx, (const quint8 * const *)pFrame->data,
                pFrame->linesize, 0, decoder->pCodecCtx->height, pFrameRGB->data,
                pFrameRGB->linesize);
        if (ret > 0) {
            QImage tmpImage(videoBuf, decoder->pCodecCtx->width, decoder->pCodecCtx->height, QImage::Format_RGB32);
            /* deep copy, otherwise when tmpImage data change, this image cannot display */
            QImage image = tmpImage.copy();
            decoder->displayVideo(image);
        }

        av_packet_unref(&packet);
    }

    av_free(videoBuf);
    av_frame_free(&pFrameRGB);
    av_frame_free(&pFrame);

    if (!decoder->isStop) {
        decoder->isStop = true;
    }

    SDL_Delay(100);

    decoder->isDecodeFinished = true;

    if (decoder->gotStop) {
        decoder->setPlayState(Decoder::STOP);
    } else {
        decoder->setPlayState(Decoder::FINISH);
    }

    qDebug() << "Video decoder finished.";

    return 0;
}

void Decoder::run()
{
    AVFormatContext *pFormatCtx;        // format context

    AVCodec *pCodec;

    AVPacket pkt, *packet = &pkt;        // packet use in decoding

    int videoIndex = -1, audioIndex = -1, subtitleIndex = -1;
    int seekIndex;

    pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&pFormatCtx, currentFile.toLocal8Bit().data(), NULL, NULL) != 0) {
        qDebug() << "Open file failed.";
        return ;
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        qDebug() << "Could't find stream infomation.";
        goto fail1;
    }

//    qDebug() << "Is real-time: " << isRealtime(pFormatCtx);

//    av_dump_format(pFormatCtx, 0, 0, 0);

    /* find video & audio stream index
     */
    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndex = i;
            qDebug() << "Find video stream.";
        }

        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIndex = i;
            qDebug() << "Find audio stream.";
        }

        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            subtitleIndex = i;
            qDebug() << "Find subtitle stream.";
        }
    }

    if (currentType == "video") {
        if (videoIndex < 0 || audioIndex < 0) {
            qDebug() << "Not support this video file.";
            goto fail1;
        }
    } else {
        if (audioIndex < 0) {
            qDebug() << "Not support this audio file.";
            goto fail1;
        }
    }

    emit gotVideoTime(pFormatCtx->duration);
    timeTotal = pFormatCtx->duration;

    if (audioDecoder->openAudio(pFormatCtx, audioIndex) < 0) {
        goto fail1;
    }

    if (currentType == "video") {
        /* find video decoder */
        pCodecCtx = avcodec_alloc_context3(NULL);
        avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoIndex]->codecpar);

        /* find video decoder */
        if ((pCodec = avcodec_find_decoder(pCodecCtx->codec_id)) == NULL) {
            qDebug() << "Video decoder not found.";
            goto fail2;
        }

        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            qDebug() << "Could not open video decoder.";
            goto fail2;
        }

        videoStream = pFormatCtx->streams[videoIndex];

        /* init image sws context */
        imgCovertCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                    pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                    AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

        SDL_CreateThread(&Decoder::videoThread, "video_thread", this);
    }

    setPlayState(Decoder::PLAYING);

    while (true) {
        if (isStop) {
            break;
        }

        /* do not read next frame & delay to release cpu utilization */
        if (isPause) {
            SDL_Delay(10);
            continue;
        }

/* this seek just use in playing music, while read finished
 * & have out of loop, then jump back to seek position
 */
seek:
        if (isSeek) {
            if (currentType == "video") {
                seekIndex = videoIndex;
            } else {
                seekIndex = audioIndex;
            }

            AVRational aVRational = av_get_time_base_q();
            seekPos = av_rescale_q(seekPos, aVRational, pFormatCtx->streams[seekIndex]->time_base);

            if (av_seek_frame(pFormatCtx, seekIndex, seekPos, AVSEEK_FLAG_BACKWARD) < 0) {
                qDebug() << "Seek failed.";

            } else {
                audioDecoder->emptyQueue();
                audioDecoder->packetEnqueue(&seekPacket);

                if (currentType == "video") {
                    videoQueue.empty();
                    videoQueue.enqueue(&seekPacket);
                    videoClk = 0;
                }
            }

            isSeek = false;
        }

        if (currentType == "video") {
            if (videoQueue.queueSize() > 512) {
                SDL_Delay(10);
                continue;
            }
        }

        /* judge haven't reall all frame */
        if (av_read_frame(pFormatCtx, packet) < 0){
            qDebug() << "Read file completed.";
            isReadFinished = true;
            emit readFinished();
            SDL_Delay(10);
            break;
        }

        if (packet->stream_index == videoIndex && currentType == "video") {
            videoQueue.enqueue(packet); // video stream
        } else if (packet->stream_index == audioIndex) {
            audioDecoder->packetEnqueue(packet); // audio stream
        } else if (packet->stream_index == subtitleIndex) {
            av_packet_unref(packet);    // subtitle stream
        } else {
            av_packet_unref(packet);
        }
    }

    while (!isStop) {
        /* use in just audio play */
        if (isSeek) {
            goto seek;
        }

        SDL_Delay(100);
    }

    /* close audio device */
    audioDecoder->closeAudio();

    if (currentType == "video") {
        sws_freeContext(imgCovertCtx);
fail2:

        avcodec_close(pCodecCtx);
        avcodec_free_context(&pCodecCtx);
    }
fail1:
    avformat_close_input(&pFormatCtx);
    avformat_free_context(pFormatCtx);

    isReadFinished = true;

    if (currentType == "music") {
        setPlayState(Decoder::STOP);
    }

    qDebug() << "Main decoder finished.";
}
