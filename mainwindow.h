#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVector>
#include <QList>

#include "decoder.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    void paintEvent(QPaintEvent *event);
    void closeEvent(QCloseEvent *event);
    void changeEvent(QEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);

    void initUI();
    void initFFmpeg();
    void initSlot();
    void initTray();

    QString fileType(QString file);
    void addPathVideoToList(QString path);
    void playVideo(QString file);
    void playNext();
    void playPreview();
    void showPlayMenu();

    void setHide(QWidget *widget);
    void showControl(bool show);

    inline QString getFilenameFromPath(QString path);

    Ui::MainWindow *ui;

    Decoder *decoder;
    QList<QString> playList;    // list to stroe video files in same path

    QString currentPlay;        // current playing video file path
    QString currentPlayType;

    QTimer *menuTimer;      // menu hide timer
    QTimer *progressTimer;  // check play progress timer

    bool menuIsVisible;     // switch to control show/hide menu
    bool isKeepAspectRatio; // switch to control image scale whether keep aspect ratio

    QImage image;

    bool autoPlay;          // switch to control whether to continue to playing other file
    bool loopPlay;          // switch to control whether to continue to playing same file
    bool closeNotExit;      // switch to control click exit button not exit but hide

    Decoder::PlayState playState;

    QVector<QWidget *> hideVector;

    qint64 timeTotal;

    int seekInterval;

private slots:
    void buttonClickSlot();
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void timerSlot();
    void editText();
    void seekProgress(int value);
    void videoTime(qint64 time);
    void playStateChanged(Decoder::PlayState state);

    /* right click menu slot */
    void setFullScreen();
    void setKeepRatio();
    void setAutoPlay();
    void setLoopPlay();
    void saveCurrentFrame();

    void showVideo(QImage);

signals:
    void selectedVideoFile(QString file, QString type);
    void stopVideo();
    void pauseVideo();

};

#endif // MAINWINDOW_H
