﻿#include "NetAccess.h"
#include <QUrl>
#include <QTimer>

bool NetworkAccess::DownloadFile(const QString strUrl, const QString strSaveAs, QVariant data)
{
    DownloadInfo info;
    info.strUrl = strUrl;
    info.strSaveFilePath = strSaveAs;
    info.data = data;

    if (downloadQueue.isEmpty())
        QTimer::singleShot(0, this, SLOT(startNextDownload()));

    downloadQueue.enqueue(info);

    return true;
}

bool NetworkAccess::SyncDownloadString(const QString strUrl, QString &strSaveBuffer, QUrlQuery query)
{
    QUrl url(strUrl);
    if(!query.isEmpty())
        url.setQuery(query);

    QNetworkRequest request;
    request.setUrl(url);
    request.setRawHeader("Accept","*/*");
    request.setRawHeader("Accept-Language","zh-CN");
    request.setRawHeader("User-Agent","Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0)");
    request.setRawHeader("Content-Type","application/x-www-form-urlencoded");
    request.setRawHeader("Accept-Encoding","deflate");
    //request.setRawHeader("Host","i.y.qq.com");
    //request.setRawHeader("Cookie","qqmusic_fromtag=3; qqmusic_miniversion=57; qqmusic_version=12;");

    QNetworkAccessManager manager;
    QNetworkReply *reply= manager.get(request);
    QEventLoop loop;
    connect(reply,SIGNAL(finished()),&loop,SLOT(quit()));
    connect(reply,SIGNAL(error(QNetworkReply::NetworkError)), &loop,SLOT(quit()));
    loop.exec();

    bool bRet = false;
    if(reply->error()==QNetworkReply::NoError)
    {
        QByteArray byte=reply->readAll();
        strSaveBuffer = QString(byte);
        bRet = true;
    }
    reply->deleteLater();

    return bRet;
}

bool NetworkAccess::SyncDownloadStringPost(const QString strUrl, QString &strSaveBuffer, QByteArray queryData)
{
    QNetworkRequest request;
    request.setUrl(strUrl);
    request.setRawHeader("Content-Type","application/x-www-form-urlencoded");

    QNetworkAccessManager manager;
    QNetworkReply *reply= manager.post(request,queryData);
    QEventLoop loop;
    connect(reply,SIGNAL(finished()),&loop,SLOT(quit()));
    connect(reply,SIGNAL(error(QNetworkReply::NetworkError)), &loop,SLOT(quit()));
    loop.exec();

    bool bRet = false;
    if(reply->error()==QNetworkReply::NoError)
    {
        QByteArray byte=reply->readAll();
        strSaveBuffer = QString(byte);
        bRet = true;
    }
    reply->deleteLater();

    return bRet;
}

void NetworkAccess::replyFinished(QNetworkReply *reply){
    //获取响应的信息，状态码为200表示正常
    //        QVariant status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    //        QByteArray bytes = reply->readAll();  //获取字节
    //        QString result(bytes);  //转化为字符串
    //        qDebug()<<result;

    //无错误返回
    if(reply->error() == QNetworkReply::NoError)
    {
        qDebug()<<"success";
        QByteArray bytes = reply->readAll();  //获取字节
        *pStrBuffer =  QString(bytes);  //转化为字符串
        qDebug()<<*pStrBuffer;

        bDownloadResult = true;
    }
    else
    {
        //处理错误
        qDebug()<<"failed";
        bDownloadResult = false;
    }

    //reply->deleteLater();//要删除reply，但是不能在repyfinished里直接delete，要调用deletelater;

    //downloadDone.wakeAll();

    bDownloadFinish = true;
}

void NetworkAccess::onDownloadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    qDebug()<<"bytesSent/bytesTotal:" << bytesSent <<"/"<< bytesTotal << "\n";
}

void NetworkAccess::onReadyRead(){
    //file->write(reply->readAll());
    qDebug()<<"downloading...... \n";
}

void NetworkAccess::startNextDownload()
{
    if (downloadQueue.isEmpty()) {
        qDebug()<<("queue download finish\n");
        emit finished();
        return;
    }

    DownloadInfo downloadInfo = downloadQueue.head();

    output.setFileName(downloadInfo.strSaveFilePath);
    if (!output.open(QIODevice::WriteOnly)) {
        qDebug() << "Problem opening save file "<< downloadInfo.strSaveFilePath
                 << " for download "<< downloadInfo.strUrl;

        DownloadInfo info = downloadQueue.dequeue();
        emit(sig_fileOpenErrorWhenSave(info.data));

        startNextDownload();
        return;                 // skip this download
    }

    QNetworkRequest request(QUrl(downloadInfo.strUrl));
    currentDownload = manager.get(request);
    connect(currentDownload, SIGNAL(downloadProgress(qint64,qint64)),
            SLOT(downloadProgress(qint64,qint64)));
    connect(currentDownload, SIGNAL(finished()),
            SLOT(downloadFinished()));
    connect(currentDownload, SIGNAL(readyRead()),
            SLOT(downloadReadyRead()));

    // prepare the output
    qDebug()<< "Downloading ... "<< downloadInfo.strUrl;
    downloadTime.start();
}

void NetworkAccess::downloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    //progressBar.setStatus(bytesReceived, bytesTotal);

    // calculate the download speed
    double speed = bytesReceived * 1000.0 / downloadTime.elapsed();
    QString unit;
    if (speed < 1024) {
        unit = "bytes/sec";
    } else if (speed < 1024*1024) {
        speed /= 1024;
        unit = "kB/s";
    } else {
        speed /= 1024*1024;
        unit = "MB/s";
    }

    QString strSpeed = QString::fromLatin1("%1 %2").arg(speed, 3, 'f', 1).arg(unit);
    qDebug()<< strSpeed;

    int percent;
    if(bytesReceived == 0|| bytesTotal == 0)
        percent = 0;
    else
        percent =bytesReceived * 100 /bytesTotal;

    emit sig_progressChanged(strSpeed, percent ,downloadQueue.head().data);
}

void NetworkAccess::downloadFinished()
{
    //progressBar.clear();
    output.close();

    bool bDeleteFile = false;
    QString fileToDelete;

    if (currentDownload->error()) {
        // download failed
        fprintf(stderr, "Failed: %s\n", qPrintable(currentDownload->errorString()));

        DownloadInfo info = downloadQueue.dequeue();
        bDeleteFile = true;
        fileToDelete = info.strSaveFilePath;
        emit sig_netErrorWhenDownload(info.data);
    } else {

        int statusCode = currentDownload->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        QString strUrl = currentDownload->attribute(QNetworkRequest::RedirectionTargetAttribute).toString();

        if(statusCode== 302)
        {
            qDebug()<<"redirect to Location: " + strUrl;

            if(strUrl == "http://music.163.com/404")
            {
                DownloadInfo info = downloadQueue.dequeue();
                bDeleteFile = true;
                fileToDelete = info.strSaveFilePath;
                emit sig_finishDownload(info.data,DOWNLOAD_FINISH_STATUS::NETEASE_MUSIC_NOT_FOUND );
            }
            else
                downloadQueue.head().strUrl = strUrl;
        }
        else
        {
            qDebug()<<("Succeeded  code->")<< statusCode;
            DownloadInfo info = downloadQueue.dequeue();
            emit sig_finishDownload(info.data, DOWNLOAD_FINISH_STATUS::NORMAL);
        }

        //++downloadedCount;
    }

    if(bDeleteFile)
        QFile::remove(fileToDelete);

    currentDownload->deleteLater();
    startNextDownload();
}

void NetworkAccess::downloadReadyRead()
{
    output.write(currentDownload->readAll());
}

