/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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
#include "config.h"

#include "account.h"
#include "accountstate.h"
#include "folder.h"
#include "folderman.h"
#include "logger.h"
#include "configfile.h"
#include "networkjobs.h"
#include "syncjournalfilerecord.h"
#include "syncresult.h"
#include "utility.h"
#include "clientproxy.h"
#include "syncengine.h"
#include "syncrunfilelog.h"
#include "socketapi.h"
#include "theme.h"
#include "filesystem.h"
#include "excludedfiles.h"

#include "creds/abstractcredentials.h"

#include <QTimer>
#include <QUrl>
#include <QDir>
#include <QSettings>

#include <QMessageBox>
#include <QPushButton>

namespace OCC {

Q_LOGGING_CATEGORY(lcFolder, "gui.folder", QtInfoMsg)

Folder::Folder(const FolderDefinition &definition,
    AccountState *accountState,
    QObject *parent)
    : QObject(parent)
    , _accountState(accountState)
    , _definition(definition)
    , _csyncUnavail(false)
    , _proxyDirty(true)
    , _lastSyncDuration(0)
    , _consecutiveFailingSyncs(0)
    , _consecutiveFollowUpSyncs(0)
    , _journal(_definition.absoluteJournalPath())
    , _fileLog(new SyncRunFileLog)
    , _saveBackwardsCompatible(false)
{
    _timeSinceLastSyncStart.start();
    _timeSinceLastSyncDone.start();

    SyncResult::Status status = SyncResult::NotYetStarted;
    if (definition.paused) {
        status = SyncResult::Paused;
    }
    _syncResult.setStatus(status);

    // check if the local path exists
    checkLocalPath();

    _syncResult.setFolder(_definition.alias);

    _engine.reset(new SyncEngine(_accountState->account(), path(), remotePath(), &_journal));
    // pass the setting if hidden files are to be ignored, will be read in csync_update
    _engine->setIgnoreHiddenFiles(_definition.ignoreHiddenFiles);

    if (!setIgnoredFiles())
        qCWarning(lcFolder, "Could not read system exclude file");

    connect(_accountState.data(), SIGNAL(isConnectedChanged()), this, SIGNAL(canSyncChanged()));
    connect(_engine.data(), SIGNAL(rootEtag(QString)), this, SLOT(etagRetreivedFromSyncEngine(QString)));

    connect(_engine.data(), SIGNAL(started()), SLOT(slotSyncStarted()), Qt::QueuedConnection);
    connect(_engine.data(), SIGNAL(finished(bool)), SLOT(slotSyncFinished(bool)), Qt::QueuedConnection);
    connect(_engine.data(), SIGNAL(csyncUnavailable()), SLOT(slotCsyncUnavailable()), Qt::QueuedConnection);

    //direct connection so the message box is blocking the sync.
    connect(_engine.data(), SIGNAL(aboutToRemoveAllFiles(SyncFileItem::Direction, bool *)),
        SLOT(slotAboutToRemoveAllFiles(SyncFileItem::Direction, bool *)));
    connect(_engine.data(), SIGNAL(aboutToRestoreBackup(bool *)),
        SLOT(slotAboutToRestoreBackup(bool *)));
    connect(_engine.data(), SIGNAL(transmissionProgress(ProgressInfo)), this, SLOT(slotTransmissionProgress(ProgressInfo)));
    connect(_engine.data(), SIGNAL(itemCompleted(const SyncFileItemPtr &)),
        this, SLOT(slotItemCompleted(const SyncFileItemPtr &)));
    connect(_engine.data(), SIGNAL(newBigFolder(QString, bool)),
        this, SLOT(slotNewBigFolderDiscovered(QString, bool)));
    connect(_engine.data(), SIGNAL(seenLockedFile(QString)), FolderMan::instance(), SLOT(slotSyncOnceFileUnlocks(QString)));
    connect(_engine.data(), SIGNAL(aboutToPropagate(SyncFileItemVector &)),
        SLOT(slotLogPropagationStart()));
    connect(_engine.data(), &SyncEngine::syncError, this, &Folder::slotSyncError);

    _scheduleSelfTimer.setSingleShot(true);
    _scheduleSelfTimer.setInterval(SyncEngine::minimumFileAgeForUpload);
    connect(&_scheduleSelfTimer, SIGNAL(timeout()),
        SLOT(slotScheduleThisFolder()));
}

Folder::~Folder()
{
    // Reset then engine first as it will abort and try to access members of the Folder
    _engine.reset();
}


void Folder::checkLocalPath()
{
    const QFileInfo fi(_definition.localPath);
    _canonicalLocalPath = fi.canonicalFilePath();
    if (_canonicalLocalPath.isEmpty()) {
        qCWarning(lcFolder) << "Broken symlink:" << _definition.localPath;
        _canonicalLocalPath = _definition.localPath;
    } else if (!_canonicalLocalPath.endsWith('/')) {
        _canonicalLocalPath.append('/');
    }

    if (fi.isDir() && fi.isReadable()) {
        qCDebug(lcFolder) << "Checked local path ok";
    } else {
        // Check directory again
        if (!FileSystem::fileExists(_definition.localPath, fi)) {
            _syncResult.appendErrorString(tr("Local folder %1 does not exist.").arg(_definition.localPath));
            _syncResult.setStatus(SyncResult::SetupError);
        } else if (!fi.isDir()) {
            _syncResult.appendErrorString(tr("%1 should be a folder but is not.").arg(_definition.localPath));
            _syncResult.setStatus(SyncResult::SetupError);
        } else if (!fi.isReadable()) {
            _syncResult.appendErrorString(tr("%1 is not readable.").arg(_definition.localPath));
            _syncResult.setStatus(SyncResult::SetupError);
        }
    }
}

QString Folder::shortGuiRemotePathOrAppName() const
{
    if (remotePath().length() > 0 && remotePath() != QLatin1String("/")) {
        QString a = QFile(remotePath()).fileName();
        if (a.startsWith('/')) {
            a = a.remove(0, 1);
        }
        return a;
    } else {
        return Theme::instance()->appNameGUI();
    }
}

QString Folder::alias() const
{
    return _definition.alias;
}

QString Folder::path() const
{
    return _canonicalLocalPath;
}

QString Folder::shortGuiLocalPath() const
{
    QString p = _definition.localPath;
    QString home = QDir::homePath();
    if (!home.endsWith('/')) {
        home.append('/');
    }
    if (p.startsWith(home)) {
        p = p.mid(home.length());
    }
    if (p.length() > 1 && p.endsWith('/')) {
        p.chop(1);
    }
    return QDir::toNativeSeparators(p);
}


bool Folder::ignoreHiddenFiles()
{
    bool re(_definition.ignoreHiddenFiles);
    return re;
}

void Folder::setIgnoreHiddenFiles(bool ignore)
{
    _definition.ignoreHiddenFiles = ignore;
}

QString Folder::cleanPath() const
{
    QString cleanedPath = QDir::cleanPath(_canonicalLocalPath);

    if (cleanedPath.length() == 3 && cleanedPath.endsWith(":/"))
        cleanedPath.remove(2, 1);

    return cleanedPath;
}

bool Folder::isBusy() const
{
    return _engine->isSyncRunning();
}

QString Folder::remotePath() const
{
    return _definition.targetPath;
}

QUrl Folder::remoteUrl() const
{
    return Utility::concatUrlPath(_accountState->account()->davUrl(), remotePath());
}

bool Folder::syncPaused() const
{
    return _definition.paused;
}

bool Folder::canSync() const
{
    return !syncPaused() && accountState()->isConnected();
}

void Folder::setSyncPaused(bool paused)
{
    if (paused == _definition.paused) {
        return;
    }

    _definition.paused = paused;
    saveToSettings();

    if (!paused) {
        setSyncState(SyncResult::NotYetStarted);
    } else {
        setSyncState(SyncResult::Paused);
    }
    emit syncPausedChanged(this, paused);
    emit syncStateChange();
    emit canSyncChanged();
}

void Folder::setSyncState(SyncResult::Status state)
{
    _syncResult.setStatus(state);
}

SyncResult Folder::syncResult() const
{
    return _syncResult;
}

void Folder::prepareToSync()
{
    _syncResult.reset();
    _syncResult.setStatus(SyncResult::NotYetStarted);
}

void Folder::slotRunEtagJob()
{
    qCInfo(lcFolder) << "Trying to check" << remoteUrl().toString() << "for changes via ETag check. (time since last sync:" << (_timeSinceLastSyncDone.elapsed() / 1000) << "s)";

    AccountPtr account = _accountState->account();

    if (_requestEtagJob) {
        qCInfo(lcFolder) << remoteUrl().toString() << "has ETag job queued, not trying to sync";
        return;
    }

    if (!canSync()) {
        qCInfo(lcFolder) << "Not syncing.  :" << remoteUrl().toString() << _definition.paused << AccountState::stateString(_accountState->state());
        return;
    }

    // Do the ordinary etag check for the root folder and schedule a
    // sync if it's different.

    _requestEtagJob = new RequestEtagJob(account, remotePath(), this);
    _requestEtagJob->setTimeout(60 * 1000);
    // check if the etag is different when retrieved
    QObject::connect(_requestEtagJob, SIGNAL(etagRetreived(QString)), this, SLOT(etagRetreived(QString)));
    FolderMan::instance()->slotScheduleETagJob(alias(), _requestEtagJob);
    // The _requestEtagJob is auto deleting itself on finish. Our guard pointer _requestEtagJob will then be null.
}

void Folder::etagRetreived(const QString &etag)
{
    // re-enable sync if it was disabled because network was down
    FolderMan::instance()->setSyncEnabled(true);

    if (_lastEtag != etag) {
        qCInfo(lcFolder) << "Compare etag with previous etag: last:" << _lastEtag << ", received:" << etag << "-> CHANGED";
        _lastEtag = etag;
        slotScheduleThisFolder();
    }

    _accountState->tagLastSuccessfullETagRequest();
}

void Folder::etagRetreivedFromSyncEngine(const QString &etag)
{
    qCInfo(lcFolder) << "Root etag from during sync:" << etag;
    accountState()->tagLastSuccessfullETagRequest();
    _lastEtag = etag;
}


void Folder::showSyncResultPopup()
{
    if (_syncResult.firstItemNew()) {
        createGuiLog(_syncResult.firstItemNew()->_file, LogStatusNew, _syncResult.numNewItems());
    }
    if (_syncResult.firstItemDeleted()) {
        createGuiLog(_syncResult.firstItemDeleted()->_file, LogStatusRemove, _syncResult.numRemovedItems());
    }
    if (_syncResult.firstItemUpdated()) {
        createGuiLog(_syncResult.firstItemUpdated()->_file, LogStatusUpdated, _syncResult.numUpdatedItems());
    }

    if (_syncResult.firstItemRenamed()) {
        LogStatus status(LogStatusRename);
        // if the path changes it's rather a move
        QDir renTarget = QFileInfo(_syncResult.firstItemRenamed()->_renameTarget).dir();
        QDir renSource = QFileInfo(_syncResult.firstItemRenamed()->_file).dir();
        if (renTarget != renSource) {
            status = LogStatusMove;
        }
        createGuiLog(_syncResult.firstItemRenamed()->_originalFile, status,
            _syncResult.numRenamedItems(), _syncResult.firstItemRenamed()->_renameTarget);
    }

    if (_syncResult.firstNewConflictItem()) {
        createGuiLog(_syncResult.firstNewConflictItem()->_file, LogStatusConflict, _syncResult.numNewConflictItems());
    }
    if (int errorCount = _syncResult.numErrorItems()) {
        createGuiLog(_syncResult.firstItemError()->_file, LogStatusError, errorCount);
    }

    qCInfo(lcFolder) << "Folder sync result: " << int(_syncResult.status());
}

void Folder::createGuiLog(const QString &filename, LogStatus status, int count,
    const QString &renameTarget)
{
    if (count > 0) {
        Logger *logger = Logger::instance();

        QString file = QDir::toNativeSeparators(filename);
        QString text;

        switch (status) {
        case LogStatusRemove:
            if (count > 1) {
                text = tr("%1 and %n other file(s) have been removed.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has been removed.", "%1 names a file.").arg(file);
            }
            break;
        case LogStatusNew:
            if (count > 1) {
                text = tr("%1 and %n other file(s) have been downloaded.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has been downloaded.", "%1 names a file.").arg(file);
            }
            break;
        case LogStatusUpdated:
            if (count > 1) {
                text = tr("%1 and %n other file(s) have been updated.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has been updated.", "%1 names a file.").arg(file);
            }
            break;
        case LogStatusRename:
            if (count > 1) {
                text = tr("%1 has been renamed to %2 and %n other file(s) have been renamed.", "", count - 1).arg(file, renameTarget);
            } else {
                text = tr("%1 has been renamed to %2.", "%1 and %2 name files.").arg(file, renameTarget);
            }
            break;
        case LogStatusMove:
            if (count > 1) {
                text = tr("%1 has been moved to %2 and %n other file(s) have been moved.", "", count - 1).arg(file, renameTarget);
            } else {
                text = tr("%1 has been moved to %2.").arg(file, renameTarget);
            }
            break;
        case LogStatusConflict:
            if (count > 1) {
                text = tr("%1 has and %n other file(s) have sync conflicts.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has a sync conflict. Please check the conflict file!").arg(file);
            }
            break;
        case LogStatusError:
            if (count > 1) {
                text = tr("%1 and %n other file(s) could not be synced due to errors. See the log for details.", "", count - 1).arg(file);
            } else {
                text = tr("%1 could not be synced due to an error. See the log for details.").arg(file);
            }
            break;
        }

        if (!text.isEmpty()) {
            logger->postOptionalGuiLog(tr("Sync Activity"), text);
        }
    }
}

int Folder::slotDiscardDownloadProgress()
{
    // Delete from journal and from filesystem.
    QDir folderpath(_definition.localPath);
    QSet<QString> keep_nothing;
    const QVector<SyncJournalDb::DownloadInfo> deleted_infos =
        _journal.getAndDeleteStaleDownloadInfos(keep_nothing);
    foreach (const SyncJournalDb::DownloadInfo &deleted_info, deleted_infos) {
        const QString tmppath = folderpath.filePath(deleted_info._tmpfile);
        qCInfo(lcFolder) << "Deleting temporary file: " << tmppath;
        FileSystem::remove(tmppath);
    }
    return deleted_infos.size();
}

int Folder::downloadInfoCount()
{
    return _journal.downloadInfoCount();
}

int Folder::errorBlackListEntryCount()
{
    return _journal.errorBlackListEntryCount();
}

int Folder::slotWipeErrorBlacklist()
{
    return _journal.wipeErrorBlacklist();
}

void Folder::slotWatchedPathChanged(const QString &path)
{
// The folder watcher fires a lot of bogus notifications during
// a sync operation, both for actual user files and the database
// and log. Therefore we check notifications against operations
// the sync is doing to filter out our own changes.
#ifdef Q_OS_MAC
    Q_UNUSED(path)
// On OSX the folder watcher does not report changes done by our
// own process. Therefore nothing needs to be done here!
#else
    // Use the path to figure out whether it was our own change
    if (_engine->wasFileTouched(path)) {
        qCDebug(lcFolder) << "Changed path was touched by SyncEngine, ignoring:" << path;
        return;
    }
#endif

    // Check that the mtime actually changed.
    if (path.startsWith(this->path())) {
        auto relativePath = path.mid(this->path().size());
        auto record = _journal.getFileRecord(relativePath);
        if (record.isValid() && !FileSystem::fileChanged(path, record._fileSize, Utility::qDateTimeToTime_t(record._modtime))) {
            qCInfo(lcFolder) << "Ignoring spurious notification for file" << relativePath;
            return; // probably a spurious notification
        }
    }

    emit watchedFileChangedExternally(path);

    // Also schedule this folder for a sync, but only after some delay:
    // The sync will not upload files that were changed too recently.
    scheduleThisFolderSoon();
}

void Folder::saveToSettings() const
{
    // Remove first to make sure we don't get duplicates
    removeFromSettings();

    auto settings = _accountState->settings();

    // The folder is saved to backwards-compatible "Folders"
    // section only if it has the migrate flag set (i.e. was in
    // there before) or if the folder is the only one for the
    // given target path.
    // This ensures that older clients will not read a configuration
    // where two folders for different accounts point at the same
    // local folders.
    bool oneAccountOnly = true;
    foreach (Folder *other, FolderMan::instance()->map()) {
        if (other != this && other->cleanPath() == this->cleanPath()) {
            oneAccountOnly = false;
            break;
        }
    }

    bool compatible = _saveBackwardsCompatible || oneAccountOnly;

    if (compatible) {
        settings->beginGroup(QLatin1String("Folders"));
    } else {
        settings->beginGroup(QLatin1String("Multifolders"));
    }
    FolderDefinition::save(*settings, _definition);

    settings->sync();
    qCInfo(lcFolder) << "Saved folder" << _definition.alias << "to settings, status" << settings->status();
}

void Folder::removeFromSettings() const
{
    auto settings = _accountState->settings();
    settings->beginGroup(QLatin1String("Folders"));
    settings->remove(FolderMan::escapeAlias(_definition.alias));
    settings->endGroup();
    settings->beginGroup(QLatin1String("Multifolders"));
    settings->remove(FolderMan::escapeAlias(_definition.alias));
}

bool Folder::isFileExcludedAbsolute(const QString &fullPath) const
{
    return _engine->excludedFiles().isExcluded(fullPath, path(), _definition.ignoreHiddenFiles);
}

bool Folder::isFileExcludedRelative(const QString &relativePath) const
{
    return _engine->excludedFiles().isExcluded(path() + relativePath, path(), _definition.ignoreHiddenFiles);
}

void Folder::slotTerminateSync()
{
    qCInfo(lcFolder) << "folder " << alias() << " Terminating!";

    if (_engine->isSyncRunning()) {
        _engine->abort();

        setSyncState(SyncResult::SyncAbortRequested);
    }
}

// This removes the csync File database
// This is needed to provide a clean startup again in case another
// local folder is synced to the same ownCloud.
void Folder::wipe()
{
    QString stateDbFile = _engine->journal()->databaseFilePath();

    // Delete files that have been partially downloaded.
    slotDiscardDownloadProgress();

    //Unregister the socket API so it does not keep the ._sync_journal file open
    FolderMan::instance()->socketApi()->slotUnregisterPath(alias());
    _journal.close(); // close the sync journal

    QFile file(stateDbFile);
    if (file.exists()) {
        if (!file.remove()) {
            qCWarning(lcFolder) << "Failed to remove existing csync StateDB " << stateDbFile;
        } else {
            qCInfo(lcFolder) << "wipe: Removed csync StateDB " << stateDbFile;
        }
    } else {
        qCWarning(lcFolder) << "statedb is empty, can not remove.";
    }

    // Also remove other db related files
    QFile::remove(stateDbFile + ".ctmp");
    QFile::remove(stateDbFile + "-shm");
    QFile::remove(stateDbFile + "-wal");
    QFile::remove(stateDbFile + "-journal");

    if (canSync())
        FolderMan::instance()->socketApi()->slotRegisterPath(alias());
}

bool Folder::setIgnoredFiles()
{
    // Note: Doing this on each sync run and on Folder construction is
    // unnecessary, because _engine->excludedFiles() persists between
    // sync runs. This is not a big problem because ExcludedFiles maintains
    // a QSet of files to load.
    ConfigFile cfg;
    QString systemList = cfg.excludeFile(ConfigFile::SystemScope);
    qCInfo(lcFolder) << "Adding system ignore list to csync:" << systemList;
    _engine->excludedFiles().addExcludeFilePath(systemList);

    QString userList = cfg.excludeFile(ConfigFile::UserScope);
    if (QFile::exists(userList)) {
        qCInfo(lcFolder) << "Adding user defined ignore list to csync:" << userList;
        _engine->excludedFiles().addExcludeFilePath(userList);
    }

    return _engine->excludedFiles().reloadExcludes();
}

void Folder::setProxyDirty(bool value)
{
    _proxyDirty = value;
}

bool Folder::proxyDirty()
{
    return _proxyDirty;
}

void Folder::startSync(const QStringList &pathList)
{
    Q_UNUSED(pathList)
    if (proxyDirty()) {
        setProxyDirty(false);
    }

    if (isBusy()) {
        qCCritical(lcFolder) << "ERROR csync is still running and new sync requested.";
        return;
    }
    _csyncUnavail = false;

    _timeSinceLastSyncStart.start();
    _syncResult.setStatus(SyncResult::SyncPrepare);
    emit syncStateChange();

    qCInfo(lcFolder) << "*** Start syncing " << remoteUrl().toString() << " - client version"
                     << qPrintable(Theme::instance()->version());

    _fileLog->start(path());

    if (!setIgnoredFiles()) {
        slotSyncError(tr("Could not read system exclude file"));
        QMetaObject::invokeMethod(this, "slotSyncFinished", Qt::QueuedConnection, Q_ARG(bool, false));
        return;
    }

    setDirtyNetworkLimits();
    setSyncOptions();

    _engine->setIgnoreHiddenFiles(_definition.ignoreHiddenFiles);

    QMetaObject::invokeMethod(_engine.data(), "startSync", Qt::QueuedConnection);

    emit syncStarted();
}

void Folder::setSyncOptions()
{
    SyncOptions opt;
    ConfigFile cfgFile;

    auto newFolderLimit = cfgFile.newBigFolderSizeLimit();
    opt._newBigFolderSizeLimit = newFolderLimit.first ? newFolderLimit.second * 1000LL * 1000LL : -1; // convert from MB to B
    opt._confirmExternalStorage = cfgFile.confirmExternalStorage();

    QByteArray chunkSizeEnv = qgetenv("OWNCLOUD_CHUNK_SIZE");
    if (!chunkSizeEnv.isEmpty()) {
        opt._initialChunkSize = chunkSizeEnv.toUInt();
    } else {
        opt._initialChunkSize = cfgFile.chunkSize();
    }
    QByteArray minChunkSizeEnv = qgetenv("OWNCLOUD_MIN_CHUNK_SIZE");
    if (!minChunkSizeEnv.isEmpty()) {
        opt._minChunkSize = minChunkSizeEnv.toUInt();
    } else {
        opt._minChunkSize = cfgFile.minChunkSize();
    }
    QByteArray maxChunkSizeEnv = qgetenv("OWNCLOUD_MAX_CHUNK_SIZE");
    if (!maxChunkSizeEnv.isEmpty()) {
        opt._maxChunkSize = maxChunkSizeEnv.toUInt();
    } else {
        opt._maxChunkSize = cfgFile.maxChunkSize();
    }

    // Previously min/max chunk size values didn't exist, so users might
    // have setups where the chunk size exceeds the new min/max default
    // values. To cope with this, adjust min/max to always include the
    // initial chunk size value.
    opt._minChunkSize = qMin(opt._minChunkSize, opt._initialChunkSize);
    opt._maxChunkSize = qMax(opt._maxChunkSize, opt._initialChunkSize);

    QByteArray targetChunkUploadDurationEnv = qgetenv("OWNCLOUD_TARGET_CHUNK_UPLOAD_DURATION");
    if (!targetChunkUploadDurationEnv.isEmpty()) {
        opt._targetChunkUploadDuration = targetChunkUploadDurationEnv.toUInt();
    } else {
        opt._targetChunkUploadDuration = cfgFile.targetChunkUploadDuration();
    }

    _engine->setSyncOptions(opt);
}

void Folder::setDirtyNetworkLimits()
{
    ConfigFile cfg;
    int downloadLimit = -75; // 75%
    int useDownLimit = cfg.useDownloadLimit();
    if (useDownLimit >= 1) {
        downloadLimit = cfg.downloadLimit() * 1000;
    } else if (useDownLimit == 0) {
        downloadLimit = 0;
    }

    int uploadLimit = -75; // 75%
    int useUpLimit = cfg.useUploadLimit();
    if (useUpLimit >= 1) {
        uploadLimit = cfg.uploadLimit() * 1000;
    } else if (useUpLimit == 0) {
        uploadLimit = 0;
    }

    _engine->setNetworkLimits(uploadLimit, downloadLimit);
}

void Folder::slotSyncError(const QString &message, ErrorCategory category)
{
    _syncResult.appendErrorString(message);
    emit ProgressDispatcher::instance()->syncError(alias(), message, category);
}

void Folder::slotSyncStarted()
{
    qCInfo(lcFolder) << "#### Propagation start ####################################################";
    _syncResult.setStatus(SyncResult::SyncRunning);
    emit syncStateChange();
}

void Folder::slotCsyncUnavailable()
{
    _csyncUnavail = true;
}

void Folder::slotSyncFinished(bool success)
{
    qCInfo(lcFolder) << "Client version" << qPrintable(Theme::instance()->version())
                     << " Qt" << qVersion()
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
                     << " SSL " << QSslSocket::sslLibraryVersionString().toUtf8().data()
#endif
        ;

    bool syncError = !_syncResult.errorStrings().isEmpty();
    if (syncError) {
        qCWarning(lcFolder) << "SyncEngine finished with ERROR";
    } else {
        qCInfo(lcFolder) << "SyncEngine finished without problem.";
    }
    _fileLog->finish();
    showSyncResultPopup();

    auto anotherSyncNeeded = _engine->isAnotherSyncNeeded();

    if (syncError) {
        _syncResult.setStatus(SyncResult::Error);
    } else if (_csyncUnavail) {
        _syncResult.setStatus(SyncResult::Error);
        qCWarning(lcFolder) << "csync not available.";
    } else if (_syncResult.foundFilesNotSynced()) {
        _syncResult.setStatus(SyncResult::Problem);
    } else if (_definition.paused) {
        // Maybe the sync was terminated because the user paused the folder
        _syncResult.setStatus(SyncResult::Paused);
    } else {
        _syncResult.setStatus(SyncResult::Success);
    }

    // Count the number of syncs that have failed in a row.
    if (_syncResult.status() == SyncResult::Success
        || _syncResult.status() == SyncResult::Problem) {
        _consecutiveFailingSyncs = 0;
    } else {
        _consecutiveFailingSyncs++;
        qCInfo(lcFolder) << "the last" << _consecutiveFailingSyncs << "syncs failed";
    }

    if (_syncResult.status() == SyncResult::Success && success) {
        // Clear the white list as all the folders that should be on that list are sync-ed
        journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncWhiteList, QStringList());
    }

    emit syncStateChange();

    // The syncFinished result that is to be triggered here makes the folderman
    // clear the current running sync folder marker.
    // Lets wait a bit to do that because, as long as this marker is not cleared,
    // file system change notifications are ignored for that folder. And it takes
    // some time under certain conditions to make the file system notifications
    // all come in.
    QTimer::singleShot(200, this, SLOT(slotEmitFinishedDelayed()));

    _lastSyncDuration = _timeSinceLastSyncStart.elapsed();
    _timeSinceLastSyncDone.start();

    // Increment the follow-up sync counter if necessary.
    if (anotherSyncNeeded == ImmediateFollowUp) {
        _consecutiveFollowUpSyncs++;
        qCInfo(lcFolder) << "another sync was requested by the finished sync, this has"
                         << "happened" << _consecutiveFollowUpSyncs << "times";
    } else {
        _consecutiveFollowUpSyncs = 0;
    }

    // Maybe force a follow-up sync to take place, but only a couple of times.
    if (anotherSyncNeeded == ImmediateFollowUp && _consecutiveFollowUpSyncs <= 3) {
        // Sometimes another sync is requested because a local file is still
        // changing, so wait at least a small amount of time before syncing
        // the folder again.
        scheduleThisFolderSoon();
    }
}

void Folder::slotEmitFinishedDelayed()
{
    emit syncFinished(_syncResult);
}

// the progress comes without a folder and the valid path set. Add that here
// and hand the result over to the progress dispatcher.
void Folder::slotTransmissionProgress(const ProgressInfo &pi)
{
    emit progressInfo(pi);
    ProgressDispatcher::instance()->setProgressInfo(alias(), pi);
}

// a item is completed: count the errors and forward to the ProgressDispatcher
void Folder::slotItemCompleted(const SyncFileItemPtr &item)
{
    // add new directories or remove gone away dirs to the watcher
    if (item->_isDirectory && item->_instruction == CSYNC_INSTRUCTION_NEW) {
        FolderMan::instance()->addMonitorPath(alias(), path() + item->_file);
    }
    if (item->_isDirectory && item->_instruction == CSYNC_INSTRUCTION_REMOVE) {
        FolderMan::instance()->removeMonitorPath(alias(), path() + item->_file);
    }

    _syncResult.processCompletedItem(item);

    _fileLog->logItem(*item);
    emit ProgressDispatcher::instance()->itemCompleted(alias(), item);
}

void Folder::slotNewBigFolderDiscovered(const QString &newF, bool isExternal)
{
    auto newFolder = newF;
    if (!newFolder.endsWith(QLatin1Char('/'))) {
        newFolder += QLatin1Char('/');
    }
    auto journal = journalDb();

    // Add the entry to the blacklist if it is neither in the blacklist or whitelist already
    bool ok1, ok2;
    auto blacklist = journal->getSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, &ok1);
    auto whitelist = journal->getSelectiveSyncList(SyncJournalDb::SelectiveSyncWhiteList, &ok2);
    if (ok1 && ok2 && !blacklist.contains(newFolder) && !whitelist.contains(newFolder)) {
        blacklist.append(newFolder);
        journal->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, blacklist);
    }

    // And add the entry to the undecided list and signal the UI
    auto undecidedList = journal->getSelectiveSyncList(SyncJournalDb::SelectiveSyncUndecidedList, &ok1);
    if (ok1) {
        if (!undecidedList.contains(newFolder)) {
            undecidedList.append(newFolder);
            journal->setSelectiveSyncList(SyncJournalDb::SelectiveSyncUndecidedList, undecidedList);
            emit newBigFolderDiscovered(newFolder);
        }
        QString message = !isExternal ? (tr("A new folder larger than %1 MB has been added: %2.\n")
                                                .arg(ConfigFile().newBigFolderSizeLimit().second)
                                                .arg(newF))
                                      : (tr("A folder from an external storage has been added.\n"));
        message += tr("Please go in the settings to select it if you wish to download it.");

        auto logger = Logger::instance();
        logger->postOptionalGuiLog(Theme::instance()->appNameGUI(), message);
    }
}

void Folder::slotLogPropagationStart()
{
    _fileLog->logLap("Propagation starts");
}

void Folder::slotScheduleThisFolder()
{
    FolderMan::instance()->scheduleFolder(this);
}

void Folder::scheduleThisFolderSoon()
{
    if (!_scheduleSelfTimer.isActive()) {
        _scheduleSelfTimer.start();
    }
}

void Folder::setSaveBackwardsCompatible(bool save)
{
    _saveBackwardsCompatible = save;
}

void Folder::slotAboutToRemoveAllFiles(SyncFileItem::Direction dir, bool *cancel)
{
    ConfigFile cfgFile;
    if (!cfgFile.promptDeleteFiles())
        return;

    QString msg = dir == SyncFileItem::Down ? tr("All files in the sync folder '%1' folder were deleted on the server.\n"
                                                 "These deletes will be synchronized to your local sync folder, making such files "
                                                 "unavailable unless you have a right to restore. \n"
                                                 "If you decide to keep the files, they will be re-synced with the server if you have rights to do so.\n"
                                                 "If you decide to delete the files, they will be unavailable to you, unless you are the owner.")
                                            : tr("All the files in your local sync folder '%1' were deleted. These deletes will be "
                                                 "synchronized with your server, making such files unavailable unless restored.\n"
                                                 "Are you sure you want to sync those actions with the server?\n"
                                                 "If this was an accident and you decide to keep your files, they will be re-synced from the server.");
    QMessageBox msgBox(QMessageBox::Warning, tr("Remove All Files?"),
        msg.arg(shortGuiLocalPath()));
    msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);
    msgBox.addButton(tr("Remove all files"), QMessageBox::DestructiveRole);
    QPushButton *keepBtn = msgBox.addButton(tr("Keep files"), QMessageBox::AcceptRole);
    if (msgBox.exec() == -1) {
        *cancel = true;
        return;
    }
    *cancel = msgBox.clickedButton() == keepBtn;
    if (*cancel) {
        FileSystem::setFolderMinimumPermissions(path());
        journalDb()->clearFileTable();
        _lastEtag.clear();
        slotScheduleThisFolder();
    }
}

void Folder::slotAboutToRestoreBackup(bool *restore)
{
    QString msg =
        tr("This sync would reset the files to an earlier time in the sync folder '%1'.\n"
           "This might be because a backup was restored on the server.\n"
           "Continuing the sync as normal will cause all your files to be overwritten by an older "
           "file in an earlier state. "
           "Do you want to keep your local most recent files as conflict files?");
    QMessageBox msgBox(QMessageBox::Warning, tr("Backup detected"),
        msg.arg(shortGuiLocalPath()));
    msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);
    msgBox.addButton(tr("Normal Synchronisation"), QMessageBox::DestructiveRole);
    QPushButton *keepBtn = msgBox.addButton(tr("Keep Local Files as Conflict"), QMessageBox::AcceptRole);

    if (msgBox.exec() == -1) {
        *restore = true;
        return;
    }
    *restore = msgBox.clickedButton() == keepBtn;
}


void FolderDefinition::save(QSettings &settings, const FolderDefinition &folder)
{
    settings.beginGroup(FolderMan::escapeAlias(folder.alias));
    settings.setValue(QLatin1String("localPath"), folder.localPath);
    settings.setValue(QLatin1String("journalPath"), folder.journalPath);
    settings.setValue(QLatin1String("targetPath"), folder.targetPath);
    settings.setValue(QLatin1String("paused"), folder.paused);
    settings.setValue(QLatin1String("ignoreHiddenFiles"), folder.ignoreHiddenFiles);
    settings.endGroup();
}

bool FolderDefinition::load(QSettings &settings, const QString &alias,
    FolderDefinition *folder)
{
    settings.beginGroup(alias);
    folder->alias = FolderMan::unescapeAlias(alias);
    folder->localPath = settings.value(QLatin1String("localPath")).toString();
    folder->journalPath = settings.value(QLatin1String("journalPath")).toString();
    folder->targetPath = settings.value(QLatin1String("targetPath")).toString();
    folder->paused = settings.value(QLatin1String("paused")).toBool();
    folder->ignoreHiddenFiles = settings.value(QLatin1String("ignoreHiddenFiles"), QVariant(true)).toBool();
    settings.endGroup();

    // Old settings can contain paths with native separators. In the rest of the
    // code we assum /, so clean it up now.
    folder->localPath = prepareLocalPath(folder->localPath);

    // Target paths also have a convention
    folder->targetPath = prepareTargetPath(folder->targetPath);

    return true;
}

QString FolderDefinition::prepareLocalPath(const QString &path)
{
    QString p = QDir::fromNativeSeparators(path);
    if (!p.endsWith(QLatin1Char('/'))) {
        p.append(QLatin1Char('/'));
    }
    return p;
}

QString FolderDefinition::prepareTargetPath(const QString &path)
{
    QString p = path;
    if (p.endsWith(QLatin1Char('/'))) {
        p.chop(1);
    }
    // Doing this second ensures the empty string or "/" come
    // out as "/".
    if (!p.startsWith(QLatin1Char('/'))) {
        p.prepend(QLatin1Char('/'));
    }
    return p;
}

QString FolderDefinition::absoluteJournalPath() const
{
    return QDir(localPath).filePath(journalPath);
}

QString FolderDefinition::defaultJournalPath(AccountPtr account)
{
    return SyncJournalDb::makeDbName(localPath, account->url(), targetPath, account->credentials()->user());
}

} // namespace OCC
