#include <QFileDialog>
#include <QStandardPaths>
#include <QPainter>
#include <QCloseEvent>
#include <QEvent>
#include <QFileInfoList>
#include <QMenu>

#include <QDebug>

#include "mainwindow.h"
#include "ui_mainwindow.h"

extern "C"
{
#include "libavformat/avformat.h"
}

#define VOLUME_INT  (13)

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    decoder(new Decoder),
    menuTimer(new QTimer),
    progressTimer(new QTimer),
    menuIsVisible(true),
    isKeepAspectRatio(false),
    image(QImage(":/image/MUSIC.jpg")),
    autoPlay(true),
    loopPlay(false),
    closeNotExit(false),
    playState(Decoder::STOP),
    seekInterval(15)
{
    ui->setupUi(this);

    qRegisterMetaType<Decoder::PlayState>("Decoder::PlayState");

    menuTimer->setInterval(8000);
    menuTimer->start(5000);

    progressTimer->setInterval(500);

    initUI();
    initTray();
    initSlot();
    initFFmpeg();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initUI()
{
    this->setWindowTitle("QtPlayer");
    this->setWindowIcon(QIcon(":/image/player.ico"));
    this->centralWidget()->setMouseTracking(true);
    this->setMouseTracking(true);


    ui->titleLable->setAlignment(Qt::AlignCenter);

    ui->labelTime->setStyleSheet("background: #5FFFFFFF;");
    ui->labelTime->setText(QString("00.00.00 / 00:00:00"));

    ui->btnNext->setIcon(QIcon(":/image/next.ico"));
    ui->btnNext->setIconSize(QSize(48, 48));
    ui->btnNext->setStyleSheet("background: transparent;border:none;");

    ui->btnPreview->setIcon(QIcon(":/image/forward.ico"));
    ui->btnPreview->setIconSize(QSize(48, 48));
    ui->btnPreview->setStyleSheet("background: transparent;border:none;");

    ui->btnStop->setIcon(QIcon(":/image/stop.ico"));
    ui->btnStop->setIconSize(QSize(48, 48));
    ui->btnStop->setStyleSheet("background: transparent;border:none;");

    ui->btnPause->setIcon(QIcon(":/image/play.ico"));
    ui->btnPause->setIconSize(QSize(48, 48));
    ui->btnPause->setStyleSheet("background: transparent;border:none;");

    setHide(ui->btnOpenLocal);
    setHide(ui->btnOpenUrl);
    setHide(ui->btnStop);
    setHide(ui->btnPause);
    setHide(ui->btnNext);
    setHide(ui->btnPreview);
    setHide(ui->lineEdit);
    setHide(ui->videoProgressSlider);
    setHide(ui->labelTime);

    ui->videoProgressSlider->installEventFilter(this);
}

void MainWindow::initFFmpeg()
{
//    av_log_set_level(AV_LOG_INFO);

    avfilter_register_all();

    /* ffmpeg init */
    av_register_all();

    /* ffmpeg network init for rtsp */
    if (avformat_network_init()) {
        qDebug() << "avformat network init failed";
    }

    /* init sdl audio */
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        qDebug() << "SDL init failed";
    }
}

void MainWindow::initSlot()
{
    connect(ui->btnOpenLocal,   SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnOpenUrl,     SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnStop,        SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnPause,       SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnNext,        SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnPreview,     SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->lineEdit,       SIGNAL(cursorPositionChanged(int,int)),     this, SLOT(editText()));

    connect(menuTimer,      SIGNAL(timeout()), this, SLOT(timerSlot()));
    connect(progressTimer,  SIGNAL(timeout()), this, SLOT(timerSlot()));

    connect(ui->videoProgressSlider,    SIGNAL(sliderMoved(int)), this, SLOT(seekProgress(int)));

    connect(this, SIGNAL(selectedVideoFile(QString,QString)),   decoder, SLOT(decoderFile(QString,QString)));
    connect(this, SIGNAL(stopVideo()),                          decoder, SLOT(stopVideo()));
    connect(this, SIGNAL(pauseVideo()),                         decoder, SLOT(pauseVideo()));

    connect(decoder, SIGNAL(playStateChanged(Decoder::PlayState)),  this, SLOT(playStateChanged(Decoder::PlayState)));
    connect(decoder, SIGNAL(gotVideoTime(qint64)),                  this, SLOT(videoTime(qint64)));
    connect(decoder, SIGNAL(gotVideo(QImage)),                      this, SLOT(showVideo(QImage)));
}

void MainWindow::initTray()
{
    QSystemTrayIcon *trayIcon = new QSystemTrayIcon(this);

    trayIcon->setToolTip(tr("QtPlayer"));
    trayIcon->setIcon(QIcon(":/image/player.ico"));
    trayIcon->show();

    QAction *minimizeAction = new QAction(tr("最小化 (&I)"), this);
    connect(minimizeAction, SIGNAL(triggered()), this, SLOT(hide()));
    QAction *restoreAction = new QAction(tr("还原 (&R)"), this);
    connect(restoreAction, SIGNAL(triggered()), this, SLOT(showNormal()));
    QAction *quitAction = new QAction(tr("退出 (&Q)"), this);
    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));

    /* tray right click menu */
    QMenu *trayIconMenu = new QMenu(this);

    trayIconMenu->addAction(minimizeAction);
    trayIconMenu->addAction(restoreAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
    trayIcon->setContextMenu(trayIconMenu);

    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    int width = this->width();
    int height = this->height();


    painter.setBrush(Qt::black);
    painter.drawRect(0, 0, width, height);

    if (isKeepAspectRatio) {
        QImage img = image.scaled(QSize(width, height), Qt::KeepAspectRatio);

        /* calculate display position */
        int x = (this->width() - img.width()) / 2;
        int y = (this->height() - img.height()) / 2;

        painter.drawImage(QPoint(x, y), img);
    } else {
        QImage img = image.scaled(QSize(width, height));

        painter.drawImage(QPoint(0, 0), img);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (closeNotExit) {
        /* ignore original close event */
        event->ignore();

        /* hide window & not show in task bar */
        this->hide();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    /* judge whether is window change event */
    if (event->type() == QEvent::WindowStateChange) {
        if (this->windowState() == Qt::WindowMinimized) {
            /* hide window & not show in task bar */
            event->ignore();
            this->hide();
        } else if (this->windowState() == Qt::WindowMaximized) {

        }
    }
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if (object == ui->videoProgressSlider) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                int duration = ui->videoProgressSlider->maximum() - ui->videoProgressSlider->minimum();
                int pos = ui->videoProgressSlider->minimum() + duration * (static_cast<double>(mouseEvent->x()) / ui->videoProgressSlider->width());
                if (pos != ui->videoProgressSlider->sliderPosition()) {
                    ui->videoProgressSlider->setValue(pos);
                    decoder->seekProgress(static_cast<qint64>(pos) * 1000000);
                }
            }
        }
    }

    return QObject::eventFilter(object, event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    int progressVal;
    int volumnVal = decoder->getVolume();

    switch (event->key()) {
    case Qt::Key_Up:
        if (volumnVal + VOLUME_INT > SDL_MIX_MAXVOLUME) {
            decoder->setVolume(SDL_MIX_MAXVOLUME);
        } else {
            decoder->setVolume(volumnVal + VOLUME_INT);
        }
        break;

    case Qt::Key_Down:
        if (volumnVal - VOLUME_INT < 0) {
            decoder->setVolume(0);
        } else {
            decoder->setVolume(volumnVal - VOLUME_INT);
        }
        break;

    case Qt::Key_Left:
        if (ui->videoProgressSlider->value() > seekInterval) {
            progressVal = ui->videoProgressSlider->value() - seekInterval;
            decoder->seekProgress(static_cast<qint64>(progressVal) * 1000000);
        }
        break;

    case Qt::Key_Right:
        if (ui->videoProgressSlider->value() + seekInterval < ui->videoProgressSlider->maximum()) {
            progressVal = ui->videoProgressSlider->value() + seekInterval;
            decoder->seekProgress(static_cast<qint64>(progressVal) * 1000000);
        }
        break;

    case Qt::Key_Escape:
        showNormal();
        break;

    case Qt::Key_Space:
        emit pauseVideo();
        break;

    default:

        break;
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event);

    /* stop timer & restart it while having mouse moving */
    if (currentPlayType == "video") {
        menuTimer->stop();
        if (!menuIsVisible) {
            showControl(true);
            menuIsVisible = true;
            QApplication::setOverrideCursor(Qt::ArrowCursor);
        }
        menuTimer->start();
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() == Qt::RightButton) {
        showPlayMenu();
    } else if (event->buttons() == Qt::LeftButton) {
        emit pauseVideo();
    }
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->buttons() == Qt::LeftButton) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    }
}

void MainWindow::showPlayMenu()
{
    QMenu *menu = new QMenu;

    QAction * fullSrcAction = new QAction("全屏", this);
    fullSrcAction->setCheckable(true);
    if (isFullScreen()) {
        fullSrcAction->setChecked(true);
    }

    QAction *keepRatioAction = new QAction("视频长宽比", this);
    keepRatioAction->setCheckable(true);
    if (isKeepAspectRatio) {
        keepRatioAction->setChecked(true);
    }

    QAction *autoPlayAction = new QAction("连续播放", this);
    autoPlayAction->setCheckable(true);
    if (autoPlay) {
        autoPlayAction->setChecked(true);
    }

    QAction *loopPlayAction = new QAction("循环播放", this);
    loopPlayAction->setCheckable(true);
    if (loopPlay) {
        loopPlayAction->setChecked(true);
    }

    QAction *captureAction = new QAction("截图", this);

    connect(fullSrcAction,      SIGNAL(triggered(bool)), this, SLOT(setFullScreen()));
    connect(keepRatioAction,    SIGNAL(triggered(bool)), this, SLOT(setKeepRatio()));
    connect(autoPlayAction,     SIGNAL(triggered(bool)), this, SLOT(setAutoPlay()));
    connect(loopPlayAction,     SIGNAL(triggered(bool)), this, SLOT(setLoopPlay()));
    connect(captureAction,      SIGNAL(triggered(bool)), this, SLOT(saveCurrentFrame()));

    menu->addAction(fullSrcAction);
    menu->addAction(keepRatioAction);
    menu->addAction(autoPlayAction);
    menu->addAction(loopPlayAction);
    menu->addAction(captureAction);

    menu->exec(QCursor::pos());

    disconnect(fullSrcAction,   SIGNAL(triggered(bool)), this, SLOT(setFullScreen()));
    disconnect(keepRatioAction, SIGNAL(triggered(bool)), this, SLOT(setKeepRatio()));
    disconnect(autoPlayAction,  SIGNAL(triggered(bool)), this, SLOT(setAutoPlay()));
    disconnect(loopPlayAction,  SIGNAL(triggered(bool)), this, SLOT(setLoopPlay()));
    disconnect(captureAction,       SIGNAL(triggered(bool)), this, SLOT(saveCurrentFrame()));

    delete fullSrcAction;
    delete keepRatioAction;
    delete autoPlayAction;
    delete loopPlayAction;
    delete captureAction;
    delete menu;
}

void MainWindow::setHide(QWidget *widget)
{
    hideVector.push_back(widget);
}

void MainWindow::showControl(bool show)
{
    if (show) {
        for (QWidget *widget : hideVector) {
            widget->show();
        }
    } else {
        for (QWidget *widget : hideVector) {
            widget->hide();
        }
    }
}

inline QString MainWindow::getFilenameFromPath(QString path)
{
    return path.right(path.size() - path.lastIndexOf("/") - 1);
}

QString MainWindow::fileType(QString file)
{
    QString type;

    QString suffix = file.right(file.size() - file.lastIndexOf(".") - 1);
    if (suffix == "mp3" || suffix == "ape" || suffix == "flac" || suffix == "wav") {
        type = "music";
    } else {
        type = "video";
    }

    return type;
}

void MainWindow::addPathVideoToList(QString path)
{
    QDir dir(path);

    QRegExp rx(".*\\.(264|rmvb|flv|mp4|mov|avi|mkv|ts|wav|flac|ape|mp3)$");

    QFileInfoList list = dir.entryInfoList(QDir::Files);
    for(int i = 0; i < list.count(); i++) {
        QFileInfo fileInfo = list.at(i);

        if (rx.exactMatch(fileInfo.fileName())) {
            QString filename = getFilenameFromPath(fileInfo.fileName());
            /* avoid adding repeat file */
            if (!playList.contains(filename)) {
                playList.push_back(fileInfo.absoluteFilePath());
            }
        }
    }
}

void MainWindow::playVideo(QString file)
{
    emit stopVideo();

    currentPlay = file;
    currentPlayType = fileType(file);
    if (currentPlayType == "video") {
        menuTimer->start();
        ui->titleLable->setText("");
    } else {
        menuTimer->stop();
        if (!menuIsVisible) {
            showControl(true);
            menuIsVisible = true;
        }
        ui->titleLable->setStyleSheet("color:rgb(25, 125, 203);font-size:24px;background: transparent;");
        ui->titleLable->setText(QString("当前播放：%1").arg(getFilenameFromPath(file)));
    }

    emit selectedVideoFile(file, currentPlayType);
}

void MainWindow::playNext()
{
    int playIndex = 0;
    int videoNum = playList.size();

    if (videoNum <= 0) {
        return;
    }

    int currentIndex = playList.indexOf(currentPlay);

    if (currentIndex != videoNum - 1) {
        playIndex = currentIndex + 1;
    }

    QString nextVideo = playList.at(playIndex);

    /* check file whether exists */
    QFile file(nextVideo);
    if (!file.exists()) {
        playList.removeAt(playIndex);
        return;
    }

    playVideo(nextVideo);
}

void MainWindow::playPreview()
{
    int playIndex = 0;
    int videoNum = playList.size();
    int currentIndex = playList.indexOf(currentPlay);

    if (videoNum <= 0) {
        return;
    }

    /* if current file index greater than 0, means have priview video
     * play last index video, otherwise if this file is head,
     * play tail index video
     */
    if (currentIndex > 0) {
        playIndex = currentIndex - 1;
    } else {
        playIndex = videoNum - 1;
    }

    QString preVideo = playList.at(playIndex);

    /* check file whether exists */
    QFile file(preVideo);
    if (!file.exists()) {
        playList.removeAt(playIndex);
        return;
    }

    playVideo(preVideo);
}

/******************* slot ************************/

void MainWindow::buttonClickSlot()
{
    QString filePath;

    if (QObject::sender() == ui->btnOpenLocal) { // open local file
        filePath = QFileDialog::getOpenFileName(
                this, "选择播放文件", "/",
                "(*.264 *.mp4 *.rmvb *.avi *.mov *.flv *.mkv *.ts *.mp3 *.flac *.ape *.wav)");
        if (!filePath.isNull() && !filePath.isEmpty()) {
            playVideo(filePath);

            QString path = filePath.left(filePath.lastIndexOf("/") + 1);
            addPathVideoToList(path);
        }
    } else if (QObject::sender() == ui->btnOpenUrl) {   // open network file
        filePath = ui->lineEdit->text();
        if (!filePath.isNull() && !filePath.isEmpty()) {
            QString type = "video";
            emit selectedVideoFile(filePath, type);
        }
    } else if (QObject::sender() == ui->btnStop) {
        emit stopVideo();
    } else if (QObject::sender() == ui->btnPause) {
        emit pauseVideo();
    } else if (QObject::sender() == ui->btnPreview) {
        playPreview();
    } else if (QObject::sender() == ui->btnNext) {
        playNext();
    }
}

void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::DoubleClick:
        this->showNormal();
        this->raise();
        this->activateWindow();
        break;

    case QSystemTrayIcon::Trigger:
    default:
        break;
    }
}

void MainWindow::setFullScreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::setKeepRatio()
{
    isKeepAspectRatio = !isKeepAspectRatio;
}

void MainWindow::setAutoPlay()
{
    autoPlay = !autoPlay;
    loopPlay = false;
}

void MainWindow::setLoopPlay()
{
    loopPlay = !loopPlay;
    autoPlay = false;
}

void MainWindow::saveCurrentFrame()
{
    QString filename = QFileDialog::getSaveFileName(this, "保存截图", "/", "(*.jpg)");
    image.save(filename);
}

void MainWindow::timerSlot()
{
    if (QObject::sender() == menuTimer) {
        if (menuIsVisible && playState == Decoder::PLAYING) {
            if (isFullScreen()) {
                QApplication::setOverrideCursor(Qt::BlankCursor);
            }
            showControl(false);
            menuIsVisible = false;
        }
    } else if (QObject::sender() == progressTimer) {
        qint64 currentTime = static_cast<qint64>(decoder->getCurrentTime());
        ui->videoProgressSlider->setValue(currentTime);

        int hourCurrent = currentTime / 60 / 60;
        int minCurrent  = (currentTime / 60) % 60;
        int secCurrent  = currentTime % 60;

        int hourTotal = timeTotal / 60 / 60;
        int minTotal  = (timeTotal / 60) % 60;
        int secTotal  = timeTotal % 60;

        ui->labelTime->setText(QString("%1.%2.%3 / %4:%5:%6")
                               .arg(hourCurrent, 2, 10, QLatin1Char('0'))
                               .arg(minCurrent, 2, 10, QLatin1Char('0'))
                               .arg(secCurrent, 2, 10, QLatin1Char('0'))
                               .arg(hourTotal, 2, 10, QLatin1Char('0'))
                               .arg(minTotal, 2, 10, QLatin1Char('0'))
                               .arg(secTotal, 2, 10, QLatin1Char('0')));
    }
}

void MainWindow::seekProgress(int value)
{
    decoder->seekProgress(static_cast<qint64>(value) * 1000000);
}

void MainWindow::editText()
{
    /* forbid control hide while inputting */
    menuTimer->stop();
    menuTimer->start();
}

void MainWindow::videoTime(qint64 time)
{
    timeTotal = time / 1000000;

    ui->videoProgressSlider->setRange(0, timeTotal);

    int hour = timeTotal / 60 / 60;
    int min  = (timeTotal / 60 ) % 60;
    int sec  = timeTotal % 60;

    ui->labelTime->setText(QString("00.00.00 / %1:%2:%3").arg(hour, 2, 10, QLatin1Char('0'))
                           .arg(min, 2, 10, QLatin1Char('0'))
                           .arg(sec, 2, 10, QLatin1Char('0')));
}

void MainWindow::showVideo(QImage image)
{
    this->image = image;
    update();
}

void MainWindow::playStateChanged(Decoder::PlayState state)
{
    switch (state) {
    case Decoder::PLAYING:
        ui->btnPause->setIcon(QIcon(":/image/pause.ico"));
        playState = Decoder::PLAYING;
        progressTimer->start();
        break;

    case Decoder::STOP:
        image = QImage(":/image/MUSIC.jpg");
        ui->btnPause->setIcon(QIcon(":/image/play.ico"));
        playState = Decoder::STOP;
        progressTimer->stop();
        ui->labelTime->setText(QString("00.00.00 / 00:00:00"));
        ui->videoProgressSlider->setValue(0);
        timeTotal = 0;
        update();
        break;

    case Decoder::PAUSE:
        ui->btnPause->setIcon(QIcon(":/image/play.ico"));
        playState = Decoder::PAUSE;
        break;

    case Decoder::FINISH:
        if (autoPlay) {
            playNext();
        } else if (loopPlay) {
            emit selectedVideoFile(currentPlay, currentPlayType);
        }else {
            image = QImage(":/image/MUSIC.jpg");
            playState = Decoder::STOP;
            progressTimer->stop();
            ui->labelTime->setText(QString("00.00.00 / 00:00:00"));
            ui->videoProgressSlider->setValue(0);
            timeTotal = 0;
        }
        break;

    default:

        break;
    }
}
