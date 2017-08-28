/*
 * Copyright (C) by Olivier Goffart <ogoffart@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "config.h"
#include "propagateupload.h"
#include "owncloudpropagator_p.h"
#include "networkjobs.h"
#include "account.h"
#include "syncjournaldb.h"
#include "syncjournalfilerecord.h"
#include "utility.h"
#include "filesystem.h"
#include "propagatorjobs.h"
#include "checksums.h"
#include "syncengine.h"
#include "propagateremotedelete.h"
#include "asserts.h"

#include <QNetworkAccessManager>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstring>

namespace OCC {

Q_LOGGING_CATEGORY(lcPutJob, "sync.networkjob.put", QtInfoMsg)
Q_LOGGING_CATEGORY(lcPollJob, "sync.networkjob.poll", QtInfoMsg)
Q_LOGGING_CATEGORY(lcPropagateUpload, "sync.propagator.upload", QtInfoMsg)

/**
 * We do not want to upload files that are currently being modified.
 * To avoid that, we don't upload files that have a modification time
 * that is too close to the current time.
 *
 * This interacts with the msBetweenRequestAndSync delay in the folder
 * manager. If that delay between file-change notification and sync
 * has passed, we should accept the file for upload here.
 */
static bool fileIsStillChanging(const SyncFileItem &item)
{
    const QDateTime modtime = Utility::qDateTimeFromTime_t(item._modtime);
    const qint64 msSinceMod = modtime.msecsTo(QDateTime::currentDateTime());

    return msSinceMod < SyncEngine::minimumFileAgeForUpload
        // if the mtime is too much in the future we *do* upload the file
        && msSinceMod > -10000;
}

PUTFileJob::~PUTFileJob()
{
    // Make sure that we destroy the QNetworkReply before our _device of which it keeps an internal pointer.
    setReply(0);
}

void PUTFileJob::start()
{
    QNetworkRequest req;
    for (QMap<QByteArray, QByteArray>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        req.setRawHeader(it.key(), it.value());
    }

    req.setPriority(QNetworkRequest::LowPriority); // Long uploads must not block non-propagation jobs.

    if (_url.isValid()) {
        sendRequest("PUT", _url, req, _device);
    } else {
        sendRequest("PUT", makeDavUrl(path()), req, _device);
    }

    if (reply()->error() != QNetworkReply::NoError) {
        qCWarning(lcPutJob) << " Network error: " << reply()->errorString();
    }

    connect(reply(), SIGNAL(uploadProgress(qint64, qint64)), this, SIGNAL(uploadProgress(qint64, qint64)));
    connect(this, SIGNAL(networkActivity()), account().data(), SIGNAL(propagatorNetworkActivity()));
    _requestTimer.start();
    AbstractNetworkJob::start();
}

void PollJob::start()
{
    setTimeout(120 * 1000);
    QUrl accountUrl = account()->url();
    QUrl finalUrl = QUrl::fromUserInput(accountUrl.scheme() + QLatin1String("://") + accountUrl.authority()
        + (path().startsWith('/') ? QLatin1String("") : QLatin1String("/")) + path());
    sendRequest("GET", finalUrl);
    connect(reply(), SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(resetTimeout()));
    AbstractNetworkJob::start();
}

bool PollJob::finished()
{
    QNetworkReply::NetworkError err = reply()->error();
    if (err != QNetworkReply::NoError) {
        _item->_httpErrorCode = reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        _item->_status = classifyError(err, _item->_httpErrorCode);
        _item->_errorString = errorString();

        if (_item->_status == SyncFileItem::FatalError || _item->_httpErrorCode >= 400) {
            if (_item->_status != SyncFileItem::FatalError
                && _item->_httpErrorCode != 503) {
                SyncJournalDb::PollInfo info;
                info._file = _item->_file;
                // no info._url removes it from the database
                _journal->setPollInfo(info);
                _journal->commit("remove poll info");
            }
            emit finishedSignal();
            return true;
        }
        start();
        return false;
    }

    QByteArray jsonData = reply()->readAll().trimmed();
    qCInfo(lcPollJob) << ">" << jsonData << "<" << reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QJsonParseError jsonParseError;
    QJsonObject status = QJsonDocument::fromJson(jsonData, &jsonParseError).object();
    if (jsonParseError.error != QJsonParseError::NoError) {
        _item->_errorString = tr("Invalid JSON reply from the poll URL");
        _item->_status = SyncFileItem::NormalError;
        emit finishedSignal();
        return true;
    }

    if (status["unfinished"].toBool()) {
        start();
        return false;
    }

    _item->_errorString = status["error"].toString();
    _item->_status = _item->_errorString.isEmpty() ? SyncFileItem::Success : SyncFileItem::NormalError;
    _item->_fileId = status["fileid"].toString().toUtf8();
    _item->_etag = status["etag"].toString().toUtf8();
    _item->_responseTimeStamp = responseTimestamp();

    SyncJournalDb::PollInfo info;
    info._file = _item->_file;
    // no info._url removes it from the database
    _journal->setPollInfo(info);
    _journal->commit("remove poll info");

    emit finishedSignal();
    return true;
}

void PropagateUploadFileCommon::setDeleteExisting(bool enabled)
{
    _deleteExisting = enabled;
}


void PropagateUploadFileCommon::start()
{
    if (propagator()->_abortRequested.fetchAndAddRelaxed(0)) {
        return;
    }

    // Check if the specific file can be accessed
    if (propagator()->hasCaseClashAccessibilityProblem(_item->_file)) {
        done(SyncFileItem::NormalError, tr("File %1 cannot be uploaded because another file with the same name, differing only in case, exists").arg(QDir::toNativeSeparators(_item->_file)));
        return;
    }

    propagator()->_activeJobList.append(this);

    if (!_deleteExisting) {
        return slotComputeContentChecksum();
    }

    auto job = new DeleteJob(propagator()->account(),
        propagator()->_remoteFolder + _item->_file,
        this);
    _jobs.append(job);
    connect(job, SIGNAL(finishedSignal()), SLOT(slotComputeContentChecksum()));
    connect(job, SIGNAL(destroyed(QObject *)), SLOT(slotJobDestroyed(QObject *)));
    job->start();
}

void PropagateUploadFileCommon::slotComputeContentChecksum()
{
    if (propagator()->_abortRequested.fetchAndAddRelaxed(0)) {
        return;
    }

    const QString filePath = propagator()->getFilePath(_item->_file);

    // remember the modtime before checksumming to be able to detect a file
    // change during the checksum calculation
    _item->_modtime = FileSystem::getModTime(filePath);

#ifdef WITH_TESTING
    _stopWatch.start();
#endif

    QByteArray checksumType = contentChecksumType();

    // Maybe the discovery already computed the checksum?
    QByteArray existingChecksumType, existingChecksum;
    parseChecksumHeader(_item->_checksumHeader, &existingChecksumType, &existingChecksum);
    if (existingChecksumType == checksumType) {
        slotComputeTransmissionChecksum(checksumType, existingChecksum);
        return;
    }

    // Compute the content checksum.
    auto computeChecksum = new ComputeChecksum(this);
    computeChecksum->setChecksumType(checksumType);

    connect(computeChecksum, SIGNAL(done(QByteArray, QByteArray)),
        SLOT(slotComputeTransmissionChecksum(QByteArray, QByteArray)));
    connect(computeChecksum, SIGNAL(done(QByteArray, QByteArray)),
        computeChecksum, SLOT(deleteLater()));
    computeChecksum->start(filePath);
}

void PropagateUploadFileCommon::slotComputeTransmissionChecksum(const QByteArray &contentChecksumType, const QByteArray &contentChecksum)
{
    _item->_checksumHeader = makeChecksumHeader(contentChecksumType, contentChecksum);

#ifdef WITH_TESTING
    _stopWatch.addLapTime(QLatin1String("ContentChecksum"));
    _stopWatch.start();
#endif

    // Reuse the content checksum as the transmission checksum if possible
    const auto supportedTransmissionChecksums =
        propagator()->account()->capabilities().supportedChecksumTypes();
    if (supportedTransmissionChecksums.contains(contentChecksumType)) {
        slotStartUpload(contentChecksumType, contentChecksum);
        return;
    }

    // Compute the transmission checksum.
    auto computeChecksum = new ComputeChecksum(this);
    if (uploadChecksumEnabled()) {
        computeChecksum->setChecksumType(propagator()->account()->capabilities().uploadChecksumType());
    } else {
        computeChecksum->setChecksumType(QByteArray());
    }

    connect(computeChecksum, SIGNAL(done(QByteArray, QByteArray)),
        SLOT(slotStartUpload(QByteArray, QByteArray)));
    connect(computeChecksum, SIGNAL(done(QByteArray, QByteArray)),
        computeChecksum, SLOT(deleteLater()));
    const QString filePath = propagator()->getFilePath(_item->_file);
    computeChecksum->start(filePath);
}

void PropagateUploadFileCommon::slotStartUpload(const QByteArray &transmissionChecksumType, const QByteArray &transmissionChecksum)
{
    // Remove ourselfs from the list of active job, before any posible call to done()
    // When we start chunks, we will add it again, once for every chunks.
    propagator()->_activeJobList.removeOne(this);

    _transmissionChecksumHeader = makeChecksumHeader(transmissionChecksumType, transmissionChecksum);

    // If no checksum header was not set, reuse the transmission checksum as the content checksum.
    if (_item->_checksumHeader.isEmpty()) {
        _item->_checksumHeader = _transmissionChecksumHeader;
    }

    const QString fullFilePath = propagator()->getFilePath(_item->_file);

    if (!FileSystem::fileExists(fullFilePath)) {
        done(SyncFileItem::SoftError, tr("File Removed"));
        return;
    }
#ifdef WITH_TESTING
    _stopWatch.addLapTime(QLatin1String("TransmissionChecksum"));
#endif

    time_t prevModtime = _item->_modtime; // the _item value was set in PropagateUploadFile::start()
    // but a potential checksum calculation could have taken some time during which the file could
    // have been changed again, so better check again here.

    _item->_modtime = FileSystem::getModTime(fullFilePath);
    if (prevModtime != _item->_modtime) {
        propagator()->_anotherSyncNeeded = true;
        done(SyncFileItem::SoftError, tr("Local file changed during syncing. It will be resumed."));
        return;
    }

    quint64 fileSize = FileSystem::getSize(fullFilePath);
    _item->_size = fileSize;

    // But skip the file if the mtime is too close to 'now'!
    // That usually indicates a file that is still being changed
    // or not yet fully copied to the destination.
    if (fileIsStillChanging(*_item)) {
        propagator()->_anotherSyncNeeded = true;
        done(SyncFileItem::SoftError, tr("Local file changed during sync."));
        return;
    }

    doStartUpload();
}

UploadDevice::UploadDevice(BandwidthManager *bwm)
    : _read(0)
    , _bandwidthManager(bwm)
    , _bandwidthQuota(0)
    , _readWithProgress(0)
    , _bandwidthLimited(false)
    , _choked(false)
{
    _bandwidthManager->registerUploadDevice(this);
}


UploadDevice::~UploadDevice()
{
    if (_bandwidthManager) {
        _bandwidthManager->unregisterUploadDevice(this);
    }
}

bool UploadDevice::prepareAndOpen(const QString &fileName, qint64 start, qint64 size)
{
    _data.clear();
    _read = 0;

    QFile file(fileName);
    QString openError;
    if (!FileSystem::openAndSeekFileSharedRead(&file, &openError, start)) {
        setErrorString(openError);
        return false;
    }

    size = qBound(0ll, size, FileSystem::getSize(fileName) - start);
    _data.resize(size);
    auto read = file.read(_data.data(), size);
    if (read != size) {
        setErrorString(file.errorString());
        return false;
    }

    return QIODevice::open(QIODevice::ReadOnly);
}


qint64 UploadDevice::writeData(const char *, qint64)
{
    ASSERT(false, "write to read only device");
    return 0;
}

qint64 UploadDevice::readData(char *data, qint64 maxlen)
{
    if (_data.size() - _read <= 0) {
        // at end
        if (_bandwidthManager) {
            _bandwidthManager->unregisterUploadDevice(this);
        }
        return -1;
    }
    maxlen = qMin(maxlen, _data.size() - _read);
    if (maxlen == 0) {
        return 0;
    }
    if (isChoked()) {
        return 0;
    }
    if (isBandwidthLimited()) {
        maxlen = qMin(maxlen, _bandwidthQuota);
        if (maxlen <= 0) { // no quota
            return 0;
        }
        _bandwidthQuota -= maxlen;
    }
    std::memcpy(data, _data.data() + _read, maxlen);
    _read += maxlen;
    return maxlen;
}

void UploadDevice::slotJobUploadProgress(qint64 sent, qint64 t)
{
    if (sent == 0 || t == 0) {
        return;
    }
    _readWithProgress = sent;
}

bool UploadDevice::atEnd() const
{
    return _read >= _data.size();
}

qint64 UploadDevice::size() const
{
    return _data.size();
}

qint64 UploadDevice::bytesAvailable() const
{
    return _data.size() - _read + QIODevice::bytesAvailable();
}

// random access, we can seek
bool UploadDevice::isSequential() const
{
    return false;
}

bool UploadDevice::seek(qint64 pos)
{
    if (!QIODevice::seek(pos)) {
        return false;
    }
    if (pos < 0 || pos > _data.size()) {
        return false;
    }
    _read = pos;
    return true;
}

void UploadDevice::giveBandwidthQuota(qint64 bwq)
{
    if (!atEnd()) {
        _bandwidthQuota = bwq;
        QMetaObject::invokeMethod(this, "readyRead", Qt::QueuedConnection); // tell QNAM that we have quota
    }
}

void UploadDevice::setBandwidthLimited(bool b)
{
    _bandwidthLimited = b;
    QMetaObject::invokeMethod(this, "readyRead", Qt::QueuedConnection);
}

void UploadDevice::setChoked(bool b)
{
    _choked = b;
    if (!_choked) {
        QMetaObject::invokeMethod(this, "readyRead", Qt::QueuedConnection);
    }
}

void PropagateUploadFileCommon::startPollJob(const QString &path)
{
    PollJob *job = new PollJob(propagator()->account(), path, _item,
        propagator()->_journal, propagator()->_localDir, this);
    connect(job, SIGNAL(finishedSignal()), SLOT(slotPollFinished()));
    SyncJournalDb::PollInfo info;
    info._file = _item->_file;
    info._url = path;
    info._modtime = _item->_modtime;
    propagator()->_journal->setPollInfo(info);
    propagator()->_journal->commit("add poll info");
    propagator()->_activeJobList.append(this);
    job->start();
}

void PropagateUploadFileCommon::slotPollFinished()
{
    PollJob *job = qobject_cast<PollJob *>(sender());
    ASSERT(job);

    propagator()->_activeJobList.removeOne(this);

    if (job->_item->_status != SyncFileItem::Success) {
        _finished = true;
        done(job->_item->_status, job->_item->_errorString);
        return;
    }

    finalize();
}

void PropagateUploadFileCommon::checkResettingErrors()
{
    if (_item->_httpErrorCode == 412
        || propagator()->account()->capabilities().httpErrorCodesThatResetFailingChunkedUploads().contains(_item->_httpErrorCode)) {
        auto uploadInfo = propagator()->_journal->getUploadInfo(_item->_file);
        uploadInfo._errorCount += 1;
        if (uploadInfo._errorCount > 3) {
            qCInfo(lcPropagateUpload) << "Reset transfer of" << _item->_file
                                      << "due to repeated error" << _item->_httpErrorCode;
            uploadInfo = SyncJournalDb::UploadInfo();
        } else {
            qCInfo(lcPropagateUpload) << "Error count for maybe-reset error" << _item->_httpErrorCode
                                      << "on file" << _item->_file
                                      << "is" << uploadInfo._errorCount;
        }
        propagator()->_journal->setUploadInfo(_item->_file, uploadInfo);
        propagator()->_journal->commit("Upload info");
    }
}

void PropagateUploadFileCommon::commonErrorHandling(AbstractNetworkJob *job)
{
    QByteArray replyContent;
    QString errorString = job->errorStringParsingBody(&replyContent);
    qCDebug(lcPropagateUpload) << replyContent; // display the XML error in the debug

    if (_item->_httpErrorCode == 412) {
        // Precondition Failed: Either an etag or a checksum mismatch.

        // Maybe the bad etag is in the database, we need to clear the
        // parent folder etag so we won't read from DB next sync.
        propagator()->_journal->avoidReadFromDbOnNextSync(_item->_file);
        propagator()->_anotherSyncNeeded = true;
    }

    // Ensure errors that should eventually reset the chunked upload are tracked.
    checkResettingErrors();

    SyncFileItem::Status status = classifyError(job->reply()->error(), _item->_httpErrorCode,
        &propagator()->_anotherSyncNeeded);

    if (_item->_httpErrorCode == 507) {
        // Insufficient remote storage.
        _item->_errorMayBeBlacklisted = true;
        status = SyncFileItem::BlacklistedError;
        errorString = tr("Upload of %1 exceeds the quota for the folder").arg(Utility::octetsToString(_item->_size));
        emit propagator()->insufficientRemoteStorage();
    }

    abortWithError(status, errorString);
}

void PropagateUploadFileCommon::slotJobDestroyed(QObject *job)
{
    _jobs.erase(std::remove(_jobs.begin(), _jobs.end(), job), _jobs.end());
}

void PropagateUploadFileCommon::abort()
{
    foreach (auto *job, _jobs) {
        if (job->reply()) {
            job->reply()->abort();
        }
    }
}

// This function is used whenever there is an error occuring and jobs might be in progress
void PropagateUploadFileCommon::abortWithError(SyncFileItem::Status status, const QString &error)
{
    _finished = true;
    abort();
    done(status, error);
}

QMap<QByteArray, QByteArray> PropagateUploadFileCommon::headers()
{
    QMap<QByteArray, QByteArray> headers;
    headers["OC-Async"] = "1";
    headers["Content-Type"] = "application/octet-stream";
    headers["X-OC-Mtime"] = QByteArray::number(qint64(_item->_modtime));

    if (_item->_file.contains(".sys.admin#recall#")) {
        // This is a file recall triggered by the admin.  Note: the
        // recall list file created by the admin and downloaded by the
        // client (.sys.admin#recall#) also falls into this category
        // (albeit users are not supposed to mess up with it)

        // We use a special tag header so that the server may decide to store this file away in some admin stage area
        // And not directly in the user's area (which would trigger redownloads etc).
        headers["OC-Tag"] = ".sys.admin#recall#";
    }

    if (!_item->_etag.isEmpty() && _item->_etag != "empty_etag"
        && _item->_instruction != CSYNC_INSTRUCTION_NEW // On new files never send a If-Match
        && _item->_instruction != CSYNC_INSTRUCTION_TYPE_CHANGE
        && !_deleteExisting) {
        // We add quotes because the owncloud server always adds quotes around the etag, and
        //  csync_owncloud.c's owncloud_file_id always strips the quotes.
        headers["If-Match"] = '"' + _item->_etag + '"';
    }
    return headers;
}

void PropagateUploadFileCommon::finalize()
{
    _finished = true;

    if (!propagator()->_journal->setFileRecord(SyncJournalFileRecord(*_item, propagator()->getFilePath(_item->_file)))) {
        done(SyncFileItem::FatalError, tr("Error writing metadata to the database"));
        return;
    }
    // Remove from the progress database:
    propagator()->_journal->setUploadInfo(_item->_file, SyncJournalDb::UploadInfo());
    propagator()->_journal->commit("upload file start");

    done(SyncFileItem::Success);
}
}
