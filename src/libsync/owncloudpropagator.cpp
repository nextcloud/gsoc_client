/*
 * Copyright (C) by Olivier Goffart <ogoffart@owncloud.com>
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#include "owncloudpropagator.h"
#include "syncjournaldb.h"
#include "syncjournalfilerecord.h"
#include "propagatedownload.h"
#include "propagateupload.h"
#include "propagateremotedelete.h"
#include "propagateremotemove.h"
#include "propagateremotemkdir.h"
#include "propagatorjobs.h"
#include "configfile.h"
#include "utility.h"
#include "account.h"
#include "asserts.h"

#ifdef Q_OS_WIN
#include <windef.h>
#include <winbase.h>
#endif

#include <QStack>
#include <QFileInfo>
#include <QDir>
#include <QLoggingCategory>
#include <QTimer>
#include <QObject>
#include <QTimerEvent>
#include <qmath.h>

namespace OCC {

Q_LOGGING_CATEGORY(lcPropagator, "sync.propagator", QtInfoMsg)
Q_LOGGING_CATEGORY(lcDirectory, "sync.propagator.directory", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCleanupPolls, "sync.propagator.cleanuppolls", QtInfoMsg)

qint64 criticalFreeSpaceLimit()
{
    qint64 value = 50 * 1000 * 1000LL;

    static bool hasEnv = false;
    static qint64 env = qgetenv("OWNCLOUD_CRITICAL_FREE_SPACE_BYTES").toLongLong(&hasEnv);
    if (hasEnv) {
        value = env;
    }

    return qBound(0LL, value, freeSpaceLimit());
}

qint64 freeSpaceLimit()
{
    qint64 value = 250 * 1000 * 1000LL;

    static bool hasEnv = false;
    static qint64 env = qgetenv("OWNCLOUD_FREE_SPACE_BYTES").toLongLong(&hasEnv);
    if (hasEnv) {
        value = env;
    }

    return value;
}

OwncloudPropagator::~OwncloudPropagator()
{
}


int OwncloudPropagator::maximumActiveTransferJob()
{
    if (_downloadLimit.fetchAndAddAcquire(0) != 0 || _uploadLimit.fetchAndAddAcquire(0) != 0) {
        // disable parallelism when there is a network limit.
        return 1;
    }
    return qMin(3, qCeil(hardMaximumActiveJob() / 2.));
}

/* The maximum number of active jobs in parallel  */
int OwncloudPropagator::hardMaximumActiveJob()
{
    static int max = qgetenv("OWNCLOUD_MAX_PARALLEL").toUInt();
    if (max)
        return max;
    if (_account->isHttp2Supported())
        return 20;
    return 6; // (Qt cannot do more anyway)
}

PropagateItemJob::~PropagateItemJob()
{
    if (auto p = propagator()) {
        // Normally, every job should clean itself from the _activeJobList. So this should not be
        // needed. But if a job has a bug or is deleted before the network jobs signal get received,
        // we might risk end up with dangling pointer in the list which may cause crashes.
        p->_activeJobList.removeAll(this);
    }
}

static time_t getMinBlacklistTime()
{
    return qMax(qgetenv("OWNCLOUD_BLACKLIST_TIME_MIN").toInt(),
        25); // 25 seconds
}

static time_t getMaxBlacklistTime()
{
    int v = qgetenv("OWNCLOUD_BLACKLIST_TIME_MAX").toInt();
    if (v > 0)
        return v;
    return 24 * 60 * 60; // 1 day
}

/** Creates a blacklist entry, possibly taking into account an old one.
 *
 * The old entry may be invalid, then a fresh entry is created.
 */
static SyncJournalErrorBlacklistRecord createBlacklistEntry(
    const SyncJournalErrorBlacklistRecord &old, const SyncFileItem &item)
{
    auto entry = SyncJournalErrorBlacklistRecord::fromSyncFileItem(item);
    entry._retryCount = old._retryCount + 1;

    static time_t minBlacklistTime(getMinBlacklistTime());
    static time_t maxBlacklistTime(qMax(getMaxBlacklistTime(), minBlacklistTime));

    // The factor of 5 feels natural: 25s, 2 min, 10 min, ~1h, ~5h, ~24h
    entry._ignoreDuration = old._ignoreDuration * 5;

    if (item._httpErrorCode == 403) {
        qCWarning(lcPropagator) << "Probably firewall error: " << item._httpErrorCode << ", blacklisting up to 1h only";
        entry._ignoreDuration = qMin(entry._ignoreDuration, time_t(60 * 60));

    } else if (item._httpErrorCode == 413 || item._httpErrorCode == 415) {
        qCWarning(lcPropagator) << "Fatal Error condition" << item._httpErrorCode << ", maximum blacklist ignore time!";
        entry._ignoreDuration = maxBlacklistTime;
    }

    entry._ignoreDuration = qBound(minBlacklistTime, entry._ignoreDuration, maxBlacklistTime);

    if (item._status == SyncFileItem::SoftError) {
        // Track these errors, but don't actively suppress them.
        entry._ignoreDuration = 0;
    }

    if (item._httpErrorCode == 507) {
        entry._errorCategory = SyncJournalErrorBlacklistRecord::InsufficientRemoteStorage;
    }

    return entry;
}

/** Updates, creates or removes a blacklist entry for the given item.
 *
 * May adjust the status or item._errorString.
 */
static void blacklistUpdate(SyncJournalDb *journal, SyncFileItem &item)
{
    SyncJournalErrorBlacklistRecord oldEntry = journal->errorBlacklistEntry(item._file);

    bool mayBlacklist =
        item._errorMayBeBlacklisted // explicitly flagged for blacklisting
        || ((item._status == SyncFileItem::NormalError
                || item._status == SyncFileItem::SoftError)
               && item._httpErrorCode != 0 // or non-local error
               );

    // No new entry? Possibly remove the old one, then done.
    if (!mayBlacklist) {
        if (oldEntry.isValid()) {
            journal->wipeErrorBlacklistEntry(item._file);
        }
        return;
    }

    auto newEntry = createBlacklistEntry(oldEntry, item);
    journal->setErrorBlacklistEntry(newEntry);

    // Suppress the error if it was and continues to be blacklisted.
    // An ignoreDuration of 0 mean we're tracking the error, but not actively
    // suppressing it.
    if (item._hasBlacklistEntry && newEntry._ignoreDuration > 0) {
        item._status = SyncFileItem::BlacklistedError;

        qCInfo(lcPropagator) << "blacklisting " << item._file
                             << " for " << newEntry._ignoreDuration
                             << ", retry count " << newEntry._retryCount;

        return;
    }

    // Some soft errors might become louder on repeat occurrence
    if (item._status == SyncFileItem::SoftError
        && newEntry._retryCount > 1) {
        qCWarning(lcPropagator) << "escalating soft error on " << item._file
                                << " to normal error, " << item._httpErrorCode;
        item._status = SyncFileItem::NormalError;
        return;
    }
}

void PropagateItemJob::done(SyncFileItem::Status statusArg, const QString &errorString)
{
    _item->_status = statusArg;

    _state = Finished;
    if (_item->_isRestoration) {
        if (_item->_status == SyncFileItem::Success
            || _item->_status == SyncFileItem::Conflict) {
            _item->_status = SyncFileItem::Restoration;
        } else {
            _item->_errorString += tr("; Restoration Failed: %1").arg(errorString);
        }
    } else {
        if (_item->_errorString.isEmpty()) {
            _item->_errorString = errorString;
        }
    }

    if (propagator()->_abortRequested.fetchAndAddRelaxed(0) && (_item->_status == SyncFileItem::NormalError
                                                                   || _item->_status == SyncFileItem::FatalError)) {
        // an abort request is ongoing. Change the status to Soft-Error
        _item->_status = SyncFileItem::SoftError;
    }

    // Blacklist handling
    switch (_item->_status) {
    case SyncFileItem::SoftError:
    case SyncFileItem::FatalError:
    case SyncFileItem::NormalError:
    case SyncFileItem::BlacklistedError:
        // Check the blacklist, possibly adjusting the item (including its status)
        // but not if this status comes from blacklisting in the first place
        if (!(_item->_status == SyncFileItem::BlacklistedError
                && _item->_instruction == CSYNC_INSTRUCTION_IGNORE)) {
            blacklistUpdate(propagator()->_journal, *_item);
        }
        break;
    case SyncFileItem::Success:
    case SyncFileItem::Restoration:
        if (_item->_hasBlacklistEntry) {
            // wipe blacklist entry.
            propagator()->_journal->wipeErrorBlacklistEntry(_item->_file);
            // remove a blacklist entry in case the file was moved.
            if (_item->_originalFile != _item->_file) {
                propagator()->_journal->wipeErrorBlacklistEntry(_item->_originalFile);
            }
        }
        break;
    case SyncFileItem::Conflict:
    case SyncFileItem::FileIgnored:
    case SyncFileItem::NoStatus:
        // nothing
        break;
    }

    if (_item->hasErrorStatus())
        qCWarning(lcPropagator) << "Could not complete propagation of" << _item->destination() << "by" << this << "with status" << _item->_status << "and error:" << _item->_errorString;
    else
        qCInfo(lcPropagator) << "Completed propagation of" << _item->destination() << "by" << this << "with status" << _item->_status;
    emit propagator()->itemCompleted(_item);
    emit finished(_item->_status);

    if (_item->_status == SyncFileItem::FatalError) {
        // Abort all remaining jobs.
        propagator()->abort();
    }
}

/**
 * For delete or remove, check that we are not removing from a shared directory.
 * If we are, try to restore the file
 *
 * Return true if the problem is handled.
 */
bool PropagateItemJob::checkForProblemsWithShared(int httpStatusCode, const QString &msg)
{
    PropagateItemJob *newJob = NULL;

    if (httpStatusCode == 403 && propagator()->isInSharedDirectory(_item->_file)) {
        if (!_item->_isDirectory) {
            SyncFileItemPtr downloadItem(new SyncFileItem(*_item));
            if (downloadItem->_instruction == CSYNC_INSTRUCTION_NEW
                || downloadItem->_instruction == CSYNC_INSTRUCTION_TYPE_CHANGE) {
                // don't try to recover pushing new files
                return false;
            } else if (downloadItem->_instruction == CSYNC_INSTRUCTION_SYNC) {
                // we modified the file locally, just create a conflict then
                downloadItem->_instruction = CSYNC_INSTRUCTION_CONFLICT;

                // HACK to avoid continuation: See task #1448:  We do not know the _modtime from the
                //  server, at this point, so just set the current one. (rather than the one locally)
                downloadItem->_modtime = Utility::qDateTimeToTime_t(QDateTime::currentDateTime());
            } else {
                // the file was removed or renamed, just recover the old one
                downloadItem->_instruction = CSYNC_INSTRUCTION_SYNC;
            }
            downloadItem->_direction = SyncFileItem::Down;
            newJob = new PropagateDownloadFile(propagator(), downloadItem);
        } else {
            // Directories are harder to recover.
            // But just re-create the directory, next sync will be able to recover the files
            SyncFileItemPtr mkdirItem(new SyncFileItem(*_item));
            mkdirItem->_instruction = CSYNC_INSTRUCTION_NEW;
            mkdirItem->_direction = SyncFileItem::Down;
            newJob = new PropagateLocalMkdir(propagator(), mkdirItem);
            // Also remove the inodes and fileid from the db so no further renames are tried for
            // this item.
            propagator()->_journal->avoidRenamesOnNextSync(_item->_file);
            propagator()->_anotherSyncNeeded = true;
        }
        if (newJob) {
            newJob->setRestoreJobMsg(msg);
            _restoreJob.reset(newJob);
            connect(_restoreJob.data(), SIGNAL(finished(SyncFileItem::Status)),
                this, SLOT(slotRestoreJobFinished(SyncFileItem::Status)));
            QMetaObject::invokeMethod(newJob, "start");
        }
        return true;
    }
    return false;
}

void PropagateItemJob::slotRestoreJobFinished(SyncFileItem::Status status)
{
    QString msg;
    if (_restoreJob) {
        msg = _restoreJob->restoreJobMsg();
        _restoreJob->setRestoreJobMsg();
    }

    if (status == SyncFileItem::Success || status == SyncFileItem::Conflict
        || status == SyncFileItem::Restoration) {
        done(SyncFileItem::SoftError, msg);
    } else {
        done(status, tr("A file or folder was removed from a read only share, but restoring failed: %1").arg(msg));
    }
}

// ================================================================================

PropagateItemJob *OwncloudPropagator::createJob(const SyncFileItemPtr &item)
{
    bool deleteExisting = item->_instruction == CSYNC_INSTRUCTION_TYPE_CHANGE;
    switch (item->_instruction) {
    case CSYNC_INSTRUCTION_REMOVE:
        if (item->_direction == SyncFileItem::Down)
            return new PropagateLocalRemove(this, item);
        else
            return new PropagateRemoteDelete(this, item);
    case CSYNC_INSTRUCTION_NEW:
    case CSYNC_INSTRUCTION_TYPE_CHANGE:
        if (item->_isDirectory) {
            if (item->_direction == SyncFileItem::Down) {
                auto job = new PropagateLocalMkdir(this, item);
                job->setDeleteExistingFile(deleteExisting);
                return job;
            } else {
                auto job = new PropagateRemoteMkdir(this, item);
                job->setDeleteExisting(deleteExisting);
                return job;
            }
        } //fall through
    case CSYNC_INSTRUCTION_SYNC:
    case CSYNC_INSTRUCTION_CONFLICT:
        if (item->_direction != SyncFileItem::Up) {
            auto job = new PropagateDownloadFile(this, item);
            job->setDeleteExistingFolder(deleteExisting);
            return job;
        } else {
            PropagateUploadFileCommon *job = 0;
            if (item->_size > syncOptions()._initialChunkSize && account()->capabilities().chunkingNg()) {
                // Item is above _initialChunkSize, thus will be classified as to be chunked
                job = new PropagateUploadFileNG(this, item);
            } else {
                job = new PropagateUploadFileV1(this, item);
            }
            job->setDeleteExisting(deleteExisting);
            return job;
        }
    case CSYNC_INSTRUCTION_RENAME:
        if (item->_direction == SyncFileItem::Up) {
            return new PropagateRemoteMove(this, item);
        } else {
            return new PropagateLocalRename(this, item);
        }
    case CSYNC_INSTRUCTION_IGNORE:
    case CSYNC_INSTRUCTION_ERROR:
        return new PropagateIgnoreJob(this, item);
    default:
        return 0;
    }
    return 0;
}

quint64 OwncloudPropagator::smallFileSize()
{
    const quint64 smallFileSize = 100 * 1024; //default to 1 MB. Not dynamic right now.
    return smallFileSize;
}

void OwncloudPropagator::start(const SyncFileItemVector &items)
{
    Q_ASSERT(std::is_sorted(items.begin(), items.end()));

    /* This builds all the jobs needed for the propagation.
     * Each directory is a PropagateDirectory job, which contains the files in it.
     * In order to do that we loop over the items. (which are sorted by destination)
     * When we enter a directory, we can create the directory job and push it on the stack. */

    _rootJob.reset(new PropagateDirectory(this));
    QStack<QPair<QString /* directory name */, PropagateDirectory * /* job */>> directories;
    directories.push(qMakePair(QString(), _rootJob.data()));
    QVector<PropagatorJob *> directoriesToRemove;
    QString removedDirectory;
    foreach (const SyncFileItemPtr &item, items) {
        if (!removedDirectory.isEmpty() && item->_file.startsWith(removedDirectory)) {
            // this is an item in a directory which is going to be removed.
            PropagateDirectory *delDirJob = qobject_cast<PropagateDirectory *>(directoriesToRemove.first());

            if (item->_instruction == CSYNC_INSTRUCTION_REMOVE) {
                // already taken care of. (by the removal of the parent directory)

                // increase the number of subjobs that would be there.
                if (delDirJob) {
                    delDirJob->increaseAffectedCount();
                }
                continue;
            } else if (item->_isDirectory
                && (item->_instruction == CSYNC_INSTRUCTION_NEW
                       || item->_instruction == CSYNC_INSTRUCTION_TYPE_CHANGE)) {
                // create a new directory within a deleted directory? That can happen if the directory
                // etag was not fetched properly on the previous sync because the sync was aborted
                // while uploading this directory (which is now removed).  We can ignore it.
                if (delDirJob) {
                    delDirJob->increaseAffectedCount();
                }
                continue;
            } else if (item->_instruction == CSYNC_INSTRUCTION_IGNORE) {
                continue;
            } else if (item->_instruction == CSYNC_INSTRUCTION_RENAME) {
                // all is good, the rename will be executed before the directory deletion
            } else {
                qCWarning(lcPropagator) << "WARNING:  Job within a removed directory?  This should not happen!"
                                        << item->_file << item->_instruction;
            }
        }

        while (!item->destination().startsWith(directories.top().first)) {
            directories.pop();
        }

        if (item->_isDirectory) {
            PropagateDirectory *dir = new PropagateDirectory(this, item);

            if (item->_instruction == CSYNC_INSTRUCTION_TYPE_CHANGE
                && item->_direction == SyncFileItem::Up) {
                // Skip all potential uploads to the new folder.
                // Processing them now leads to problems with permissions:
                // checkForPermissions() has already run and used the permissions
                // of the file we're about to delete to decide whether uploading
                // to the new dir is ok...
                foreach (const SyncFileItemPtr &item2, items) {
                    if (item2->destination().startsWith(item->destination() + "/")) {
                        item2->_instruction = CSYNC_INSTRUCTION_NONE;
                        _anotherSyncNeeded = true;
                    }
                }
            }

            if (item->_instruction == CSYNC_INSTRUCTION_REMOVE) {
                // We do the removal of directories at the end, because there might be moves from
                // these directories that will happen later.
                directoriesToRemove.prepend(dir);
                removedDirectory = item->_file + "/";

                // We should not update the etag of parent directories of the removed directory
                // since it would be done before the actual remove (issue #1845)
                // NOTE: Currently this means that we don't update those etag at all in this sync,
                //       but it should not be a problem, they will be updated in the next sync.
                for (int i = 0; i < directories.size(); ++i) {
                    if (directories[i].second->_item->_instruction == CSYNC_INSTRUCTION_UPDATE_METADATA)
                        directories[i].second->_item->_instruction = CSYNC_INSTRUCTION_NONE;
                }
            } else {
                PropagateDirectory *currentDirJob = directories.top().second;
                currentDirJob->appendJob(dir);
            }
            directories.push(qMakePair(item->destination() + "/", dir));
        } else {
            if (item->_instruction == CSYNC_INSTRUCTION_TYPE_CHANGE) {
                // will delete directories, so defer execution
                directoriesToRemove.prepend(createJob(item));
                removedDirectory = item->_file + "/";
            } else {
                directories.top().second->appendTask(item);
            }
        }
    }

    foreach (PropagatorJob *it, directoriesToRemove) {
        _rootJob->appendJob(it);
    }

    connect(_rootJob.data(), SIGNAL(finished(SyncFileItem::Status)), this, SLOT(emitFinished(SyncFileItem::Status)));

    scheduleNextJob();
}

const SyncOptions &OwncloudPropagator::syncOptions() const
{
    return _syncOptions;
}

void OwncloudPropagator::setSyncOptions(const SyncOptions &syncOptions)
{
    _syncOptions = syncOptions;
    _chunkSize = syncOptions._initialChunkSize;
}

// ownCloud server  < 7.0 did not had permissions so we need some other euristics
// to detect wrong doing in a Shared directory
bool OwncloudPropagator::isInSharedDirectory(const QString &file)
{
    bool re = false;
    if (_remoteFolder.startsWith(QLatin1String("Shared"))) {
        // The Shared directory is synced as its own sync connection
        re = true;
    } else {
        if (file.startsWith("Shared/") || file == "Shared") {
            // The whole ownCloud is synced and Shared is always a top dir
            re = true;
        }
    }
    return re;
}

int OwncloudPropagator::httpTimeout()
{
    static int timeout = 0;
    if (!timeout) {
        timeout = qgetenv("OWNCLOUD_TIMEOUT").toUInt();
        if (timeout == 0) {
            ConfigFile cfg;
            timeout = cfg.timeout();
        }
    }
    return timeout;
}

bool OwncloudPropagator::localFileNameClash(const QString &relFile)
{
    bool re = false;
    const QString file(_localDir + relFile);

    if (!file.isEmpty() && Utility::fsCasePreserving()) {
#ifdef Q_OS_MAC
        QFileInfo fileInfo(file);
        if (!fileInfo.exists()) {
            re = false;
            qCWarning(lcPropagator) << "No valid fileinfo";
        } else {
            // Need to normalize to composited form because of
            // https://bugreports.qt-project.org/browse/QTBUG-39622
            const QString cName = fileInfo.canonicalFilePath().normalized(QString::NormalizationForm_C);
            bool equal = (file == cName);
            re = (!equal && !cName.endsWith(relFile, Qt::CaseSensitive));
        }
#elif defined(Q_OS_WIN)
        const QString file(_localDir + relFile);
        qCDebug(lcPropagator) << "CaseClashCheck for " << file;
        WIN32_FIND_DATA FindFileData;
        HANDLE hFind;

        hFind = FindFirstFileW((wchar_t *)file.utf16(), &FindFileData);
        if (hFind == INVALID_HANDLE_VALUE) {
            // returns false.
        } else {
            QString realFileName = QString::fromWCharArray(FindFileData.cFileName);
            FindClose(hFind);

            if (!file.endsWith(realFileName, Qt::CaseSensitive)) {
                qCWarning(lcPropagator) << "Detected case clash between" << file << "and" << realFileName;
                re = true;
            }
        }
#else
        // On Linux, the file system is case sensitive, but this code is useful for testing.
        // Just check that there is no other file with the same name and different casing.
        QFileInfo fileInfo(file);
        const QString fn = fileInfo.fileName();
        QStringList list = fileInfo.dir().entryList(QStringList() << fn);
        if (list.count() > 1 || (list.count() == 1 && list[0] != fn)) {
            re = true;
        }
#endif
    }
    return re;
}

bool OwncloudPropagator::hasCaseClashAccessibilityProblem(const QString &relfile)
{
#ifdef Q_OS_WIN
    bool result = false;
    const QString file(_localDir + relfile);
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind;

    hFind = FindFirstFileW(reinterpret_cast<const wchar_t *>(file.utf16()), &FindFileData);
    if (hFind != INVALID_HANDLE_VALUE) {
        QString firstFile = QString::fromWCharArray(FindFileData.cFileName);
        if (FindNextFile(hFind, &FindFileData)) {
            QString secondFile = QString::fromWCharArray(FindFileData.cFileName);
            // This extra check shouldn't be necessary, but ensures that there
            // are two different filenames that are identical when case is ignored.
            if (firstFile != secondFile
                && QString::compare(firstFile, secondFile, Qt::CaseInsensitive) == 0) {
                result = true;
                qCWarning(lcPropagator) << "Found two filepaths that only differ in case: " << firstFile << secondFile;
            }
        }
        FindClose(hFind);
    }
    return result;
#else
    Q_UNUSED(relfile);
    return false;
#endif
}

QString OwncloudPropagator::getFilePath(const QString &tmp_file_name) const
{
    return _localDir + tmp_file_name;
}

void OwncloudPropagator::scheduleNextJob()
{
    QTimer::singleShot(0, this, SLOT(scheduleNextJobImpl()));
}

void OwncloudPropagator::scheduleNextJobImpl()
{
    // TODO: If we see that the automatic up-scaling has a bad impact we
    // need to check how to avoid this.
    // Down-scaling on slow networks? https://github.com/owncloud/client/issues/3382
    // Making sure we do up/down at same time? https://github.com/owncloud/client/issues/1633

    if (_activeJobList.count() < maximumActiveTransferJob()) {
        if (_rootJob->scheduleSelfOrChild()) {
            scheduleNextJob();
        }
    } else if (_activeJobList.count() < hardMaximumActiveJob()) {
        int likelyFinishedQuicklyCount = 0;
        // NOTE: Only counts the first 3 jobs! Then for each
        // one that is likely finished quickly, we can launch another one.
        // When a job finishes another one will "move up" to be one of the first 3 and then
        // be counted too.
        for (int i = 0; i < maximumActiveTransferJob() && i < _activeJobList.count(); i++) {
            if (_activeJobList.at(i)->isLikelyFinishedQuickly()) {
                likelyFinishedQuicklyCount++;
            }
        }
        if (_activeJobList.count() < maximumActiveTransferJob() + likelyFinishedQuicklyCount) {
            qCDebug(lcPropagator) << "Can pump in another request! activeJobs =" << _activeJobList.count();
            if (_rootJob->scheduleSelfOrChild()) {
                scheduleNextJob();
            }
        }
    }
}

void OwncloudPropagator::reportProgress(const SyncFileItem &item, quint64 bytes)
{
    emit progress(item, bytes);
}

AccountPtr OwncloudPropagator::account() const
{
    return _account;
}

OwncloudPropagator::DiskSpaceResult OwncloudPropagator::diskSpaceCheck() const
{
    const qint64 freeBytes = Utility::freeDiskSpace(_localDir);
    if (freeBytes < 0) {
        return DiskSpaceOk;
    }

    if (freeBytes < criticalFreeSpaceLimit()) {
        return DiskSpaceCritical;
    }

    if (freeBytes - _rootJob->committedDiskSpace() < freeSpaceLimit()) {
        return DiskSpaceFailure;
    }

    return DiskSpaceOk;
}

// ================================================================================

PropagatorJob::PropagatorJob(OwncloudPropagator *propagator)
    : QObject(propagator)
    , _state(NotYetStarted)
{
}

OwncloudPropagator *PropagatorJob::propagator() const
{
    return qobject_cast<OwncloudPropagator *>(parent());
}

// ================================================================================

PropagatorJob::JobParallelism PropagatorCompositeJob::parallelism()
{
    // If any of the running sub jobs is not parallel, we have to wait
    for (int i = 0; i < _runningJobs.count(); ++i) {
        if (_runningJobs.at(i)->parallelism() != FullParallelism) {
            return _runningJobs.at(i)->parallelism();
        }
    }
    return FullParallelism;
}


bool PropagatorCompositeJob::scheduleSelfOrChild()
{
    if (_state == Finished) {
        return false;
    }

    // Start the composite job
    if (_state == NotYetStarted) {
        _state = Running;
    }

    // Ask all the running composite jobs if they have something new to schedule.
    for (int i = 0; i < _runningJobs.size(); ++i) {
        ASSERT(_runningJobs.at(i)->_state == Running);

        if (possiblyRunNextJob(_runningJobs.at(i))) {
            return true;
        }

        // If any of the running sub jobs is not parallel, we have to cancel the scheduling
        // of the rest of the list and wait for the blocking job to finish and schedule the next one.
        auto paral = _runningJobs.at(i)->parallelism();
        if (paral == WaitForFinished) {
            return false;
        }
    }

    // Now it's our turn, check if we have something left to do.
    if (!_jobsToDo.isEmpty()) {
        PropagatorJob *nextJob = _jobsToDo.first();
        _jobsToDo.remove(0);
        _runningJobs.append(nextJob);
        return possiblyRunNextJob(nextJob);
    }
    while (!_tasksToDo.isEmpty()) {
        SyncFileItemPtr nextTask = _tasksToDo.first();
        _tasksToDo.remove(0);
        PropagatorJob *job = propagator()->createJob(nextTask);
        if (!job) {
            qCWarning(lcDirectory) << "Useless task found for file" << nextTask->destination() << "instruction" << nextTask->_instruction;
            continue;
        }

        _runningJobs.append(job);
        return possiblyRunNextJob(job);
    }

    // If neither us or our children had stuff left to do we could hang. Make sure
    // we mark this job as finished so that the propagator can schedule a new one.
    if (_jobsToDo.isEmpty() && _tasksToDo.isEmpty() && _runningJobs.isEmpty()) {
        // Our parent jobs are already iterating over their running jobs, post to the event loop
        // to avoid removing ourself from that list while they iterate.
        QMetaObject::invokeMethod(this, "finalize", Qt::QueuedConnection);
    }
    return false;
}

void PropagatorCompositeJob::slotSubJobFinished(SyncFileItem::Status status)
{
    PropagatorJob *subJob = static_cast<PropagatorJob *>(sender());
    ASSERT(subJob);

    // Delete the job and remove it from our list of jobs.
    subJob->deleteLater();
    int i = _runningJobs.indexOf(subJob);
    ASSERT(i >= 0);
    _runningJobs.remove(i);

    if (status == SyncFileItem::FatalError
        || status == SyncFileItem::NormalError
        || status == SyncFileItem::SoftError) {
        _hasError = status;
    }

    if (_jobsToDo.isEmpty() && _tasksToDo.isEmpty() && _runningJobs.isEmpty()) {
        finalize();
    } else {
        propagator()->scheduleNextJob();
    }
}

void PropagatorCompositeJob::finalize()
{
    // The propagator will do parallel scheduling and this could be posted
    // multiple times on the event loop, ignore the duplicate calls.
    if (_state == Finished)
        return;

    _state = Finished;
    emit finished(_hasError == SyncFileItem::NoStatus ? SyncFileItem::Success : _hasError);
}

qint64 PropagatorCompositeJob::committedDiskSpace() const
{
    qint64 needed = 0;
    foreach (PropagatorJob *job, _runningJobs) {
        needed += job->committedDiskSpace();
    }
    return needed;
}

// ================================================================================

PropagateDirectory::PropagateDirectory(OwncloudPropagator *propagator, const SyncFileItemPtr &item)
    : PropagatorJob(propagator)
    , _item(item)
    , _firstJob(propagator->createJob(item))
    , _subJobs(propagator)
{
    if (_firstJob) {
        connect(_firstJob.data(), SIGNAL(finished(SyncFileItem::Status)), this, SLOT(slotFirstJobFinished(SyncFileItem::Status)));
    }
    connect(&_subJobs, SIGNAL(finished(SyncFileItem::Status)), this, SLOT(slotSubJobsFinished(SyncFileItem::Status)));
}

PropagatorJob::JobParallelism PropagateDirectory::parallelism()
{
    // If any of the non-finished sub jobs is not parallel, we have to wait
    if (_firstJob && _firstJob->parallelism() != FullParallelism) {
        return WaitForFinished;
    }
    if (_subJobs.parallelism() != FullParallelism) {
        return WaitForFinished;
    }
    return FullParallelism;
}


bool PropagateDirectory::scheduleSelfOrChild()
{
    if (_state == Finished) {
        return false;
    }

    if (_state == NotYetStarted) {
        _state = Running;
    }

    if (_firstJob && _firstJob->_state == NotYetStarted) {
        return _firstJob->scheduleSelfOrChild();
    }

    if (_firstJob && _firstJob->_state == Running) {
        // Don't schedule any more job until this is done.
        return false;
    }

    return _subJobs.scheduleSelfOrChild();
}

void PropagateDirectory::slotFirstJobFinished(SyncFileItem::Status status)
{
    _firstJob.take()->deleteLater();

    if (status != SyncFileItem::Success && status != SyncFileItem::Restoration) {
        if (_state != Finished) {
            abort();
            _state = Finished;
            emit finished(status);
        }
        return;
    }

    propagator()->scheduleNextJob();
}

void PropagateDirectory::slotSubJobsFinished(SyncFileItem::Status status)
{
    if (!_item->isEmpty() && status == SyncFileItem::Success) {
        if (!_item->_renameTarget.isEmpty()) {
            if (_item->_instruction == CSYNC_INSTRUCTION_RENAME
                && _item->_originalFile != _item->_renameTarget) {
                // Remove the stale entries from the database.
                propagator()->_journal->deleteFileRecord(_item->_originalFile, true);
            }

            _item->_file = _item->_renameTarget;
        }

        // For new directories we always want to update the etag once
        // the directory has been propagated. Otherwise the directory
        // could appear locally without being added to the database.
        if (_item->_instruction == CSYNC_INSTRUCTION_RENAME
            || _item->_instruction == CSYNC_INSTRUCTION_NEW
            || _item->_instruction == CSYNC_INSTRUCTION_UPDATE_METADATA) {
            if (PropagateRemoteMkdir *mkdir = qobject_cast<PropagateRemoteMkdir *>(_firstJob.data())) {
                // special case from MKDIR, get the fileId from the job there
                if (_item->_fileId.isEmpty() && !mkdir->_item->_fileId.isEmpty()) {
                    _item->_fileId = mkdir->_item->_fileId;
                }
            }
            SyncJournalFileRecord record(*_item, propagator()->_localDir + _item->_file);
            bool ok = propagator()->_journal->setFileRecordMetadata(record);
            if (!ok) {
                status = _item->_status = SyncFileItem::FatalError;
                _item->_errorString = tr("Error writing metadata to the database");
                qCWarning(lcDirectory) << "Error writing to the database for file" << _item->_file;
            }
        }
    }
    _state = Finished;
    emit finished(status);
}

// ================================================================================

CleanupPollsJob::~CleanupPollsJob()
{
}

void CleanupPollsJob::start()
{
    if (_pollInfos.empty()) {
        emit finished();
        deleteLater();
        return;
    }

    auto info = _pollInfos.first();
    _pollInfos.pop_front();
    SyncJournalFileRecord record = _journal->getFileRecord(info._file);
    SyncFileItemPtr item(new SyncFileItem(record.toSyncFileItem()));
    if (record.isValid()) {
        PollJob *job = new PollJob(_account, info._url, item, _journal, _localPath, this);
        connect(job, SIGNAL(finishedSignal()), SLOT(slotPollFinished()));
        job->start();
    }
}

void CleanupPollsJob::slotPollFinished()
{
    PollJob *job = qobject_cast<PollJob *>(sender());
    ASSERT(job);
    if (job->_item->_status == SyncFileItem::FatalError) {
        emit aborted(job->_item->_errorString);
        deleteLater();
        return;
    } else if (job->_item->_status != SyncFileItem::Success) {
        qCWarning(lcCleanupPolls) << "There was an error with file " << job->_item->_file << job->_item->_errorString;
    } else {
        if (!_journal->setFileRecord(SyncJournalFileRecord(*job->_item, _localPath + job->_item->_file))) {
            qCWarning(lcCleanupPolls) << "database error";
            job->_item->_status = SyncFileItem::FatalError;
            job->_item->_errorString = tr("Error writing metadata to the database");
            emit aborted(job->_item->_errorString);
            deleteLater();
            return;
        }
    }
    // Continue with the next entry, or finish
    start();
}
}
