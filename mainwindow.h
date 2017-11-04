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
    void playNext();
    void playPreview();

    void setHide(QWidget *widget);
    void hideControl();
    void showControl();

    Ui::MainWindow *ui;

    Decoder *decoder;
    QList<QString> playList;

    QString currentPlay;
    QString currentPlayType;

    QImage image;
    QTimer *menuTimer;
    QTimer *progressTimer;

    bool menuIsVisible;
    bool isKeepAspectRatio;

    bool autoPlay;
    bool closeNotExit;
    bool updateUI;

    Decoder::PlayState playState;

    QVector<QWidget *> hideVector;

    qint64 timeTotal;

    int seekInterval;

private slots:
    void buttonClickSlot();
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void timerSlot();
    void editText();
    void showVideo(QImage image);
    void seekProgress(int value);
    void videoTime(qint64 time);
    void playStateChanged(Decoder::PlayState state);

    /* right click menu slot */
    void setFullScreen();
    void setKeepRatio();
    void setAutoPlay();

signals:
    void selectedVideoFile(QString file, QString type);
    void stopVideo();
    void pauseVideo();

};

#endif // MAINWINDOW_H
