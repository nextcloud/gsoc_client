/*
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

#ifndef SYNCJOURNALDB_H
#define SYNCJOURNALDB_H

#include <QObject>
#include <qmutex.h>
#include <QDateTime>
#include <QHash>

#include "utility.h"
#include "ownsql.h"
#include "syncjournalfilerecord.h"

namespace OCC {
class SyncJournalFileRecord;

/**
 * @brief Class that handles the sync database
 *
 * This class is thread safe. All public functions lock the mutex.
 * @ingroup libsync
 */
class OWNCLOUDSYNC_EXPORT SyncJournalDb : public QObject
{
    Q_OBJECT
public:
    explicit SyncJournalDb(const QString &dbFilePath, QObject *parent = 0);
    virtual ~SyncJournalDb();

    /// Create a journal path for a specific configuration
    static QString makeDbName(const QString &localPath,
        const QUrl &remoteUrl,
        const QString &remotePath,
        const QString &user);

    /// Migrate a csync_journal to the new path, if necessary. Returns false on error
    static bool maybeMigrateDb(const QString &localPath, const QString &absoluteJournalPath);

    // to verify that the record could be queried successfully check
    // with SyncJournalFileRecord::isValid()
    SyncJournalFileRecord getFileRecord(const QString &filename);
    bool setFileRecord(const SyncJournalFileRecord &record);

    /// Like setFileRecord, but preserves checksums
    bool setFileRecordMetadata(const SyncJournalFileRecord &record);

    bool deleteFileRecord(const QString &filename, bool recursively = false);
    int getFileRecordCount();
    bool updateFileRecordChecksum(const QString &filename,
        const QByteArray &contentChecksum,
        const QByteArray &contentChecksumType);
    bool updateLocalMetadata(const QString &filename,
        qint64 modtime, quint64 size, quint64 inode);
    bool exists();
    void walCheckpoint();

    QString databaseFilePath() const;

    static qint64 getPHash(const QString &);

    void setErrorBlacklistEntry(const SyncJournalErrorBlacklistRecord &item);
    void wipeErrorBlacklistEntry(const QString &file);
    void wipeErrorBlacklistCategory(SyncJournalErrorBlacklistRecord::Category category);
    int wipeErrorBlacklist();
    int errorBlackListEntryCount();

    struct DownloadInfo
    {
        DownloadInfo()
            : _errorCount(0)
            , _valid(false)
        {
        }
        QString _tmpfile;
        QByteArray _etag;
        int _errorCount;
        bool _valid;
    };
    struct UploadInfo
    {
        UploadInfo()
            : _chunk(0)
            , _transferid(0)
            , _size(0)
            , _errorCount(0)
            , _valid(false)
        {
        }
        int _chunk;
        int _transferid;
        quint64 _size; //currently unused
        QDateTime _modtime;
        int _errorCount;
        bool _valid;
    };

    struct PollInfo
    {
        QString _file;
        QString _url;
        time_t _modtime;
    };

    DownloadInfo getDownloadInfo(const QString &file);
    void setDownloadInfo(const QString &file, const DownloadInfo &i);
    QVector<DownloadInfo> getAndDeleteStaleDownloadInfos(const QSet<QString> &keep);
    int downloadInfoCount();

    UploadInfo getUploadInfo(const QString &file);
    void setUploadInfo(const QString &file, const UploadInfo &i);
    // Return the list of transfer ids that were removed.
    QVector<uint> deleteStaleUploadInfos(const QSet<QString> &keep);

    SyncJournalErrorBlacklistRecord errorBlacklistEntry(const QString &);
    bool deleteStaleErrorBlacklistEntries(const QSet<QString> &keep);

    void avoidRenamesOnNextSync(const QString &path);
    void setPollInfo(const PollInfo &);
    QVector<PollInfo> getPollInfos();

    enum SelectiveSyncListType {
        /** The black list is the list of folders that are unselected in the selective sync dialog.
         * For the sync engine, those folders are considered as if they were not there, so the local
         * folders will be deleted */
        SelectiveSyncBlackList = 1,
        /** When a shared folder has a size bigger than a configured size, it is by default not sync'ed
         * Unless it is in the white list, in which case the folder is sync'ed and all its children.
         * If a folder is both on the black and the white list, the black list wins */
        SelectiveSyncWhiteList = 2,
        /** List of big sync folders that have not been confirmed by the user yet and that the UI
         * should notify about */
        SelectiveSyncUndecidedList = 3
    };
    /* return the specified list from the database */
    QStringList getSelectiveSyncList(SelectiveSyncListType type, bool *ok);
    /* Write the selective sync list (remove all other entries of that list */
    void setSelectiveSyncList(SelectiveSyncListType type, const QStringList &list);

    /**
     * Make sure that on the next sync, fileName is not read from the DB but uses the PROPFIND to
     * get the info from the server
     *
     * Specifically, this sets the md5 field of fileName and all its parents to _invalid_.
     * That causes a metadata difference and a resulting discovery from the remote for the
     * affected folders.
     *
     * Since folders in the selective sync list will not be rediscovered (csync_ftw,
     * _csync_detect_update skip them), the _invalid_ marker will stay and it. And any
     * child items in the db will be ignored when reading a remote tree from the database.
     */
    void avoidReadFromDbOnNextSync(const QString &fileName);

    /**
     * Ensures full remote discovery happens on the next sync.
     *
     * Equivalent to calling avoidReadFromDbOnNextSync() for all files.
     */
    void forceRemoteDiscoveryNextSync();

    bool postSyncCleanup(const QSet<QString> &filepathsToKeep,
        const QSet<QString> &prefixesToKeep);

    /* Because sqlite transactions are really slow, we encapsulate everything in big transactions
     * Commit will actually commit the transaction and create a new one.
     */
    void commit(const QString &context, bool startTrans = true);
    void commitIfNeededAndStartNewTransaction(const QString &context);

    void close();

    /**
     * return true if everything is correct
     */
    bool isConnected();

    /**
     * Returns the checksum type for an id.
     */
    QByteArray getChecksumType(int checksumTypeId);

    /**
     * The data-fingerprint used to detect backup
     */
    void setDataFingerprint(const QByteArray &dataFingerprint);
    QByteArray dataFingerprint();

    /**
     * Delete any file entry. This will force the next sync to re-sync everything as if it was new,
     * restoring everyfile on every remote. If a file is there both on the client and server side,
     * it will be a conflict that will be automatically resolved if the file is the same.
     */
    void clearFileTable();

private:
    bool updateDatabaseStructure();
    bool updateMetadataTableStructure();
    bool updateErrorBlacklistTableStructure();
    bool sqlFail(const QString &log, const SqlQuery &query);
    void commitInternal(const QString &context, bool startTrans = true);
    void startTransaction();
    void commitTransaction();
    QStringList tableColumns(const QString &table);
    bool checkConnect();

    // Same as forceRemoteDiscoveryNextSync but without acquiring the lock
    void forceRemoteDiscoveryNextSyncLocked();

    // Returns the integer id of the checksum type
    //
    // Returns 0 on failure and for empty checksum types.
    int mapChecksumType(const QByteArray &checksumType);

    SqlDatabase _db;
    QString _dbFile;
    QMutex _mutex; // Public functions are protected with the mutex.
    int _transaction;

    // NOTE! when adding a query, don't forget to reset it in SyncJournalDb::close
    QScopedPointer<SqlQuery> _getFileRecordQuery;
    QScopedPointer<SqlQuery> _setFileRecordQuery;
    QScopedPointer<SqlQuery> _setFileRecordChecksumQuery;
    QScopedPointer<SqlQuery> _setFileRecordLocalMetadataQuery;
    QScopedPointer<SqlQuery> _getDownloadInfoQuery;
    QScopedPointer<SqlQuery> _setDownloadInfoQuery;
    QScopedPointer<SqlQuery> _deleteDownloadInfoQuery;
    QScopedPointer<SqlQuery> _getUploadInfoQuery;
    QScopedPointer<SqlQuery> _setUploadInfoQuery;
    QScopedPointer<SqlQuery> _deleteUploadInfoQuery;
    QScopedPointer<SqlQuery> _deleteFileRecordPhash;
    QScopedPointer<SqlQuery> _deleteFileRecordRecursively;
    QScopedPointer<SqlQuery> _getErrorBlacklistQuery;
    QScopedPointer<SqlQuery> _setErrorBlacklistQuery;
    QScopedPointer<SqlQuery> _getSelectiveSyncListQuery;
    QScopedPointer<SqlQuery> _getChecksumTypeIdQuery;
    QScopedPointer<SqlQuery> _getChecksumTypeQuery;
    QScopedPointer<SqlQuery> _insertChecksumTypeQuery;
    QScopedPointer<SqlQuery> _getDataFingerprintQuery;
    QScopedPointer<SqlQuery> _setDataFingerprintQuery1;
    QScopedPointer<SqlQuery> _setDataFingerprintQuery2;

    /* This is the list of paths we called avoidReadFromDbOnNextSync on.
     * It means that they should not be written to the DB in any case since doing
     * that would write the etag and would void the purpose of avoidReadFromDbOnNextSync
     */
    QList<QString> _avoidReadFromDbOnNextSyncFilter;
};

bool OWNCLOUDSYNC_EXPORT
operator==(const SyncJournalDb::DownloadInfo &lhs,
    const SyncJournalDb::DownloadInfo &rhs);
bool OWNCLOUDSYNC_EXPORT
operator==(const SyncJournalDb::UploadInfo &lhs,
    const SyncJournalDb::UploadInfo &rhs);

} // namespace OCC
#endif // SYNCJOURNALDB_H
