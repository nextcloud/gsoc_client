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

#include "propagateremotemove.h"
#include "propagatorjobs.h"
#include "owncloudpropagator_p.h"
#include "account.h"
#include "syncjournalfilerecord.h"
#include "filesystem.h"
#include "asserts.h"
#include <QFile>
#include <QStringList>
#include <QDir>

namespace OCC {

Q_LOGGING_CATEGORY(lcMoveJob, "sync.networkjob.move", QtInfoMsg)
Q_LOGGING_CATEGORY(lcPropagateRemoteMove, "sync.propagator.remotemove", QtInfoMsg)

MoveJob::MoveJob(AccountPtr account, const QString &path,
    const QString &destination, QObject *parent)
    : AbstractNetworkJob(account, path, parent)
    , _destination(destination)
{
}

MoveJob::MoveJob(AccountPtr account, const QUrl &url, const QString &destination,
    QMap<QByteArray, QByteArray> extraHeaders, QObject *parent)
    : AbstractNetworkJob(account, QString(), parent)
    , _destination(destination)
    , _url(url)
    , _extraHeaders(extraHeaders)
{
}

void MoveJob::start()
{
    QNetworkRequest req;
    req.setRawHeader("Destination", QUrl::toPercentEncoding(_destination, "/"));
    for (auto it = _extraHeaders.constBegin(); it != _extraHeaders.constEnd(); ++it) {
        req.setRawHeader(it.key(), it.value());
    }
    if (_url.isValid()) {
        sendRequest("MOVE", _url, req);
    } else {
        sendRequest("MOVE", makeDavUrl(path()), req);
    }

    if (reply()->error() != QNetworkReply::NoError) {
        qCWarning(lcPropagateRemoteMove) << " Network error: " << reply()->errorString();
    }
    AbstractNetworkJob::start();
}


bool MoveJob::finished()
{
    qCInfo(lcMoveJob) << "MOVE of" << reply()->request().url() << "FINISHED WITH STATUS"
                      << reply()->error()
                      << (reply()->error() == QNetworkReply::NoError ? QLatin1String("") : errorString());

    emit finishedSignal();
    return true;
}

void PropagateRemoteMove::start()
{
    if (propagator()->_abortRequested.fetchAndAddRelaxed(0))
        return;

    qCDebug(lcPropagateRemoteMove) << _item->_file << _item->_renameTarget;

    QString targetFile(propagator()->getFilePath(_item->_renameTarget));

    if (_item->_file == _item->_renameTarget) {
        // The parent has been renamed already so there is nothing more to do.
        finalize();
        return;
    }
    if (_item->_file == QLatin1String("Shared")) {
        // Before owncloud 7, there was no permissions system. At the time all the shared files were
        // in a directory called "Shared" and were not supposed to be moved, otherwise bad things happened

        QString versionString = propagator()->account()->serverVersion();
        if (versionString.contains('.') && versionString.split('.')[0].toInt() < 7) {
            QString originalFile(propagator()->getFilePath(QLatin1String("Shared")));
            emit propagator()->touchedFile(originalFile);
            emit propagator()->touchedFile(targetFile);
            QString renameError;
            if (FileSystem::rename(targetFile, originalFile, &renameError)) {
                done(SyncFileItem::NormalError, tr("This folder must not be renamed. It is renamed back to its original name."));
            } else {
                done(SyncFileItem::NormalError, tr("This folder must not be renamed. Please name it back to Shared."));
            }
            return;
        }
    }

    QString destination = QDir::cleanPath(propagator()->account()->url().path() + QLatin1Char('/')
        + propagator()->account()->davPath() + propagator()->_remoteFolder + _item->_renameTarget);
    _job = new MoveJob(propagator()->account(),
        propagator()->_remoteFolder + _item->_file,
        destination, this);
    connect(_job, SIGNAL(finishedSignal()), this, SLOT(slotMoveJobFinished()));
    propagator()->_activeJobList.append(this);
    _job->start();
}

void PropagateRemoteMove::abort()
{
    if (_job && _job->reply())
        _job->reply()->abort();
}

void PropagateRemoteMove::slotMoveJobFinished()
{
    propagator()->_activeJobList.removeOne(this);

    ASSERT(_job);

    QNetworkReply::NetworkError err = _job->reply()->error();
    _item->_httpErrorCode = _job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (err != QNetworkReply::NoError) {
        if (checkForProblemsWithShared(_item->_httpErrorCode,
                tr("The file was renamed but is part of a read only share. The original file was restored."))) {
            return;
        }

        SyncFileItem::Status status = classifyError(err, _item->_httpErrorCode,
            &propagator()->_anotherSyncNeeded);
        done(status, _job->errorString());
        return;
    }

    _item->_responseTimeStamp = _job->responseTimestamp();

    if (_item->_httpErrorCode != 201) {
        // Normally we expect "201 Created"
        // If it is not the case, it might be because of a proxy or gateway intercepting the request, so we must
        // throw an error.
        done(SyncFileItem::NormalError,
            tr("Wrong HTTP code returned by server. Expected 201, but received \"%1 %2\".")
                .arg(_item->_httpErrorCode)
                .arg(_job->reply()->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString()));
        return;
    }

    finalize();
}

void PropagateRemoteMove::finalize()
{
    SyncJournalFileRecord oldRecord =
        propagator()->_journal->getFileRecord(_item->_originalFile);
    // if reading from db failed still continue hoping that deleteFileRecord
    // reopens the db successfully.
    // The db is only queried to transfer the content checksum from the old
    // to the new record. It is not a problem to skip it here.
    propagator()->_journal->deleteFileRecord(_item->_originalFile);

    SyncJournalFileRecord record(*_item, propagator()->getFilePath(_item->_renameTarget));
    record._path = _item->_renameTarget;
    if (oldRecord.isValid()) {
        record._checksumHeader = oldRecord._checksumHeader;
        if (record._fileSize != oldRecord._fileSize) {
            qCWarning(lcPropagateRemoteMove) << "File sizes differ on server vs sync journal: " << record._fileSize << oldRecord._fileSize;

            // the server might have claimed a different size, we take the old one from the DB
            record._fileSize = oldRecord._fileSize;
        }
    }

    if (!propagator()->_journal->setFileRecord(record)) {
        done(SyncFileItem::FatalError, tr("Error writing metadata to the database"));
        return;
    }

    if (_item->_isDirectory) {
        if (!adjustSelectiveSync(propagator()->_journal, _item->_file, _item->_renameTarget)) {
            done(SyncFileItem::FatalError, tr("Error writing metadata to the database"));
            return;
        }
    }

    propagator()->_journal->commit("Remote Rename");
    done(SyncFileItem::Success);
}

bool PropagateRemoteMove::adjustSelectiveSync(SyncJournalDb *journal, const QString &from_, const QString &to_)
{
    bool ok;
    // We only care about preserving the blacklist.   The white list should anyway be empty.
    // And the undecided list will be repopulated on the next sync, if there is anything too big.
    QStringList list = journal->getSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, &ok);
    if (!ok)
        return false;

    bool changed = false;
    ASSERT(!from_.endsWith(QLatin1String("/")));
    ASSERT(!to_.endsWith(QLatin1String("/")));
    QString from = from_ + QLatin1String("/");
    QString to = to_ + QLatin1String("/");

    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->startsWith(from)) {
            *it = it->replace(0, from.size(), to);
            changed = true;
        }
    }

    if (changed) {
        journal->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, list);
    }
    return true;
}
}
