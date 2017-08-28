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
#include "config.h"
#include "filesystem.h"
#include "checksums.h"
#include "syncfileitem.h"
#include "propagatorjobs.h"
#include "account.h"

#include <QLoggingCategory>
#include <qtconcurrentrun.h>

/** \file checksums.cpp
 *
 * \brief Computing and validating file checksums
 *
 * Overview
 * --------
 *
 * Checksums are used in two distinct ways during synchronization:
 *
 * - to guard uploads and downloads against data corruption
 *   (transmission checksum)
 * - to quickly check whether the content of a file has changed
 *   to avoid redundant uploads (content checksum)
 *
 * In principle both are independent and different checksumming
 * algorithms can be used. To avoid redundant computations, it can
 * make sense to use the same checksum algorithm though.
 *
 * Transmission Checksums
 * ----------------------
 *
 * The usage of transmission checksums is currently optional and needs
 * to be explicitly enabled by adding 'transmissionChecksum=TYPE' to
 * the '[General]' section of the config file.
 *
 * When enabled, the checksum will be calculated on upload and sent to
 * the server in the OC-Checksum header with the format 'TYPE:CHECKSUM'.
 *
 * On download, the header with the same name is read and if the
 * received data does not have the expected checksum, the download is
 * rejected.
 *
 * Transmission checksums guard a specific sync action and are not stored
 * in the database.
 *
 * Content Checksums
 * -----------------
 *
 * Sometimes the metadata of a local file changes while the content stays
 * unchanged. Content checksums allow the sync client to avoid uploading
 * the same data again by comparing the file's actual checksum to the
 * checksum stored in the database.
 *
 * Content checksums are not sent to the server.
 *
 * Checksum Algorithms
 * -------------------
 *
 * - Adler32 (requires zlib)
 * - MD5
 * - SHA1
 *
 */

namespace OCC {

Q_LOGGING_CATEGORY(lcChecksums, "sync.checksums", QtInfoMsg)

QByteArray makeChecksumHeader(const QByteArray &checksumType, const QByteArray &checksum)
{
    if (checksumType.isEmpty() || checksum.isEmpty())
        return QByteArray();
    QByteArray header = checksumType;
    header.append(':');
    header.append(checksum);
    return header;
}

bool parseChecksumHeader(const QByteArray &header, QByteArray *type, QByteArray *checksum)
{
    if (header.isEmpty()) {
        type->clear();
        checksum->clear();
        return true;
    }

    const auto idx = header.indexOf(':');
    if (idx < 0) {
        return false;
    }

    *type = header.left(idx);
    *checksum = header.mid(idx + 1);
    return true;
}


QByteArray parseChecksumHeaderType(const QByteArray &header)
{
    const auto idx = header.indexOf(':');
    if (idx < 0) {
        return QByteArray();
    }
    return header.left(idx);
}

bool uploadChecksumEnabled()
{
    static bool enabled = qgetenv("OWNCLOUD_DISABLE_CHECKSUM_UPLOAD").isEmpty();
    return enabled;
}

QByteArray contentChecksumType()
{
    static QByteArray type = qgetenv("OWNCLOUD_CONTENT_CHECKSUM_TYPE");
    if (type.isNull()) { // can set to "" to disable checksumming
        type = "SHA1";
    }
    return type;
}

ComputeChecksum::ComputeChecksum(QObject *parent)
    : QObject(parent)
{
}

void ComputeChecksum::setChecksumType(const QByteArray &type)
{
    _checksumType = type;
}

QByteArray ComputeChecksum::checksumType() const
{
    return _checksumType;
}

void ComputeChecksum::start(const QString &filePath)
{
    // Calculate the checksum in a different thread first.
    connect(&_watcher, SIGNAL(finished()),
        this, SLOT(slotCalculationDone()),
        Qt::UniqueConnection);
    _watcher.setFuture(QtConcurrent::run(ComputeChecksum::computeNow, filePath, checksumType()));
}

QByteArray ComputeChecksum::computeNow(const QString &filePath, const QByteArray &checksumType)
{
    if (checksumType == checkSumMD5C) {
        return FileSystem::calcMd5(filePath);
    } else if (checksumType == checkSumSHA1C) {
        return FileSystem::calcSha1(filePath);
    }
#ifdef ZLIB_FOUND
    else if (checksumType == checkSumAdlerC) {
        return FileSystem::calcAdler32(filePath);
    }
#endif
    // for an unknown checksum or no checksum, we're done right now
    if (!checksumType.isEmpty()) {
        qCWarning(lcChecksums) << "Unknown checksum type:" << checksumType;
    }
    return QByteArray();
}

void ComputeChecksum::slotCalculationDone()
{
    QByteArray checksum = _watcher.future().result();
    if (!checksum.isNull()) {
        emit done(_checksumType, checksum);
    } else {
        emit done(QByteArray(), QByteArray());
    }
}


ValidateChecksumHeader::ValidateChecksumHeader(QObject *parent)
    : QObject(parent)
{
}

void ValidateChecksumHeader::start(const QString &filePath, const QByteArray &checksumHeader)
{
    // If the incoming header is empty no validation can happen. Just continue.
    if (checksumHeader.isEmpty()) {
        emit validated(QByteArray(), QByteArray());
        return;
    }

    if (!parseChecksumHeader(checksumHeader, &_expectedChecksumType, &_expectedChecksum)) {
        qCWarning(lcChecksums) << "Checksum header malformed:" << checksumHeader;
        emit validationFailed(tr("The checksum header is malformed."));
        return;
    }

    auto calculator = new ComputeChecksum(this);
    calculator->setChecksumType(_expectedChecksumType);
    connect(calculator, SIGNAL(done(QByteArray, QByteArray)),
        SLOT(slotChecksumCalculated(QByteArray, QByteArray)));
    calculator->start(filePath);
}

void ValidateChecksumHeader::slotChecksumCalculated(const QByteArray &checksumType,
    const QByteArray &checksum)
{
    if (checksumType != _expectedChecksumType) {
        emit validationFailed(tr("The checksum header contained an unknown checksum type '%1'").arg(QString::fromLatin1(_expectedChecksumType)));
        return;
    }
    if (checksum != _expectedChecksum) {
        emit validationFailed(tr("The downloaded file does not match the checksum, it will be resumed."));
        return;
    }
    emit validated(checksumType, checksum);
}

CSyncChecksumHook::CSyncChecksumHook()
{
}

const char *CSyncChecksumHook::hook(const char *path, const char *otherChecksumHeader, void * /*this_obj*/)
{
    QByteArray type = parseChecksumHeaderType(QByteArray(otherChecksumHeader));
    if (type.isEmpty())
        return NULL;

    QByteArray checksum = ComputeChecksum::computeNow(path, type);
    if (checksum.isNull()) {
        qCWarning(lcChecksums) << "Failed to compute checksum" << type << "for" << path;
        return NULL;
    }

    QByteArray checksumHeader = makeChecksumHeader(type, checksum);
    char *result = (char *)malloc(checksumHeader.size() + 1);
    memcpy(result, checksumHeader.constData(), checksumHeader.size());
    result[checksumHeader.size()] = 0;
    return result;
}

}
