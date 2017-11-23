#include <QDebug>

#include "decoder.h"

Decoder::Decoder() :
    timeTotal(0),
    playState(STOP),
    isStop(false),
    isPause(false),
    isSeek(false),
    isReadFinished(false),
    audioDecoder(new AudioDecoder),
    filterGraph(NULL)
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
    videoIndex = -1,
    audioIndex = -1,
    subtitleIndex = -1,

    timeTotal = 0;

    isStop  = false;
    isPause = false;
    isSeek  = false;
    isReadFinished      = false;
    isDecodeFinished    = false;

    videoQueue.empty();

    audioDecoder->emptyAudioData();

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

int Decoder::initFilter()
{
    int ret;

    AVFilterInOut *out = avfilter_inout_alloc();
    AVFilterInOut *in = avfilter_inout_alloc();
    /* output format */
    enum AVPixelFormat pixFmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};

    /* free last graph */
    if (filterGraph) {
        avfilter_graph_free(&filterGraph);
    }

    filterGraph = avfilter_graph_alloc();

    /* just add filter ouptut format rgb32,
     * use for function avfilter_graph_parse_ptr()
     */
    QString filter("pp=hb/vb/dr/al");

    QString args = QString("video_size=%1x%2:pix_fmt=%3:time_base=%4/%5:pixel_aspect=%6/%7")
            .arg(pCodecCtx->width).arg(pCodecCtx->height).arg(pCodecCtx->pix_fmt)
            .arg(videoStream->time_base.num).arg(videoStream->time_base.den)
            .arg(pCodecCtx->sample_aspect_ratio.num).arg(pCodecCtx->sample_aspect_ratio.den);

    /* create source filter */
    ret = avfilter_graph_create_filter(&filterSrcCxt, avfilter_get_by_name("buffer"), "in", args.toLocal8Bit().data(), NULL, filterGraph);
    if (ret < 0) {
        qDebug() << "avfilter graph create filter failed, ret:" << ret;
        avfilter_graph_free(&filterGraph);
        goto out;
    }

    /* create sink filter */
    ret = avfilter_graph_create_filter(&filterSinkCxt, avfilter_get_by_name("buffersink"), "out", NULL, NULL, filterGraph);
    if (ret < 0) {
        qDebug() << "avfilter graph create filter failed, ret:" << ret;
        avfilter_graph_free(&filterGraph);
        goto out;
    }

    /* set sink filter ouput format */
    ret = av_opt_set_int_list(filterSinkCxt, "pix_fmts", pixFmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        qDebug() << "av opt set int list failed, ret:" << ret;
        avfilter_graph_free(&filterGraph);
        goto out;
    }

    out->name       = av_strdup("in");
    out->filter_ctx = filterSrcCxt;
    out->pad_idx    = 0;
    out->next       = NULL;

    in->name       = av_strdup("out");
    in->filter_ctx = filterSinkCxt;
    in->pad_idx    = 0;
    in->next       = NULL;

    if (filter.isEmpty() || filter.isNull()) {
        /* if no filter to add, just link source & sink */
        ret = avfilter_link(filterSrcCxt, 0, filterSinkCxt, 0);
        if (ret < 0) {
            qDebug() << "avfilter link failed, ret:" << ret;
            avfilter_graph_free(&filterGraph);
            goto out;
        }
    } else {
        /* add filter to graph */
        ret = avfilter_graph_parse_ptr(filterGraph, filter.toLatin1().data(), &in, &out, NULL);
        if (ret < 0) {
            qDebug() << "avfilter graph parse ptr failed, ret:" << ret;
            avfilter_graph_free(&filterGraph);
            goto out;
        }
    }

    /* check validity and configure all the links and formats in the graph */
    if ((ret = avfilter_graph_config(filterGraph, NULL)) < 0) {
        qDebug() << "avfilter graph config failed, ret:" << ret;
        avfilter_graph_free(&filterGraph);
    }

out:
    avfilter_inout_free(&out);
    avfilter_inout_free(&in);

    return ret;
}

void Decoder::decoderFile(QString file, QString type)
{
//    qDebug() << "Current state:" << playState;
    qDebug() << "File name:" << file << ", type:" << type;
    if (playState != STOP) {
        isStop = true;
        while (playState != STOP) {
            SDL_Delay(10);
        }
        SDL_Delay(100);
    }

    clearData();

    SDL_Delay(100);

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
        setPlayState(Decoder::STOP);
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
        av_read_pause(pFormatCtx);
        setPlayState(PAUSE);
    } else {
        av_read_play(pFormatCtx);
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
    if (audioIndex >= 0) {
        return audioDecoder->getAudioClock();
    }

    return 0;
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
    int ret;
    double pts;
    AVPacket packet;
    Decoder *decoder = (Decoder *)arg;
    AVFrame *pFrame  = av_frame_alloc();

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

        if ((pts = pFrame->pts) == AV_NOPTS_VALUE) {
            pts = 0;
        }

        pts *= av_q2d(decoder->videoStream->time_base);
        pts =  decoder->synchronize(pFrame, pts);

        if (decoder->audioIndex >= 0) {
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
        }

        if (av_buffersrc_add_frame(decoder->filterSrcCxt, pFrame) < 0) {
            qDebug() << "av buffersrc add frame failed.";
            av_packet_unref(&packet);
            continue;
        }

        if (av_buffersink_get_frame(decoder->filterSinkCxt, pFrame) < 0) {
            qDebug() << "av buffersrc get frame failed.";
            av_packet_unref(&packet);
            continue;
        } else {
            QImage tmpImage(pFrame->data[0], decoder->pCodecCtx->width, decoder->pCodecCtx->height, QImage::Format_RGB32);
            /* deep copy, otherwise when tmpImage data change, this image cannot display */
            QImage image = tmpImage.copy();
            decoder->displayVideo(image);
        }

        av_frame_unref(pFrame);
        av_packet_unref(&packet);
    }

    av_frame_free(&pFrame);

    if (!decoder->isStop) {
        decoder->isStop = true;
    }

    qDebug() << "Video decoder finished.";

    SDL_Delay(100);

    decoder->isDecodeFinished = true;

    if (decoder->gotStop) {
        decoder->setPlayState(Decoder::STOP);
    } else {
        decoder->setPlayState(Decoder::FINISH);
    }

    return 0;
}


void Decoder::run()
{
    AVCodec *pCodec;

    AVPacket pkt, *packet = &pkt;        // packet use in decoding

    int seekIndex;  
    bool realTime;

    pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&pFormatCtx, currentFile.toLocal8Bit().data(), NULL, NULL) != 0) {
        qDebug() << "Open file failed.";
        return ;
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        qDebug() << "Could't find stream infomation.";
        avformat_free_context(pFormatCtx);
        return;
    }

    realTime = isRealtime(pFormatCtx);

//    av_dump_format(pFormatCtx, 0, 0, 0);  // just use in debug output

    /* find video & audio stream index */
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
        if (videoIndex < 0) {
            qDebug() << "Not support this video file, videoIndex: " << videoIndex << ", audioIndex: " << audioIndex;
            avformat_free_context(pFormatCtx);
            return;
        }
    } else {
        if (audioIndex < 0) {
            qDebug() << "Not support this audio file.";
            avformat_free_context(pFormatCtx);
            return;
        }
    }

    if (!realTime) {
        emit gotVideoTime(pFormatCtx->duration);
        timeTotal = pFormatCtx->duration;
    } else {
        emit gotVideoTime(0);
    }
//    qDebug() << timeTotal;

    if (audioIndex >= 0) {
        if (audioDecoder->openAudio(pFormatCtx, audioIndex) < 0) {
            avformat_free_context(pFormatCtx);
            return;
        }
    }

    if (currentType == "video") {
        /* find video decoder */
        pCodecCtx = avcodec_alloc_context3(NULL);
        avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoIndex]->codecpar);

        /* find video decoder */
        if ((pCodec = avcodec_find_decoder(pCodecCtx->codec_id)) == NULL) {
            qDebug() << "Video decoder not found.";
            goto fail;
        }

        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            qDebug() << "Could not open video decoder.";
            goto fail;
        }

        videoStream = pFormatCtx->streams[videoIndex];

        if (initFilter() < 0) {
            goto fail;
        }

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
                audioDecoder->emptyAudioData();
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
//            subtitleQueue.enqueue(packet);
            av_packet_unref(packet);    // subtitle stream
        } else {
            av_packet_unref(packet);
        }
    }

//    qDebug() << isStop;
    while (!isStop) {
        /* just use at audio playing */
        if (isSeek) {
            goto seek;
        }

        SDL_Delay(100);
    }

fail:
    /* close audio device */
    if (audioIndex >= 0) {
        audioDecoder->closeAudio();
    }

    if (currentType == "video") {
        avcodec_close(pCodecCtx);
        avcodec_free_context(&pCodecCtx);
    }

    avformat_close_input(&pFormatCtx);
    avformat_free_context(pFormatCtx);

    isReadFinished = true;

    if (currentType == "music") {
        setPlayState(Decoder::STOP);
    }

    qDebug() << "Main decoder finished.";
}
