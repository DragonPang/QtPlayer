#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QObject>

class VideoDecoder : public QObject
{
    Q_OBJECT
public:
    explicit VideoDecoder(QObject *parent = nullptr);

signals:

public slots:
};

#endif // VIDEODECODER_H