/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef ACTIVITYDATA_H
#define ACTIVITYDATA_H

#include <QtCore>

#include "accountstate.h"

namespace OCC {
/**
 * @brief The ActivityLink class describes actions of an activity
 *
 * These are part of notifications which are mapped into activities.
 */

class ActivityLink
{
public:
    QString    _label;
    QString    _link;
    QByteArray _verb;
    bool       _isPrimary;
};

/* ==================================================================== */

/**
 * @brief ActivityFile Structure
 * @ingroup gui
 *
 * contains information about a file of an activity.
 * Can handle the thumbnail and stuff later.
 */
class ActivityFile
{
public:
    enum FileType {Unknown, File, Directory};
    explicit ActivityFile();
    explicit ActivityFile( const QString& file );

    void setType( FileType type );
    QString relativePath() const;
    QString fullPath( const QString _accountName ) const;

private:
    QString _relFileName;
    FileType _type;
};

/* ==================================================================== */

/**
 * @brief Activity Structure
 * @ingroup gui
 *
 * contains all the information describing a single activity.
 */

class Activity
{
public:
    typedef QPair<qlonglong, QString> Identifier;

    enum Type {
        ActivityType,
        NotificationType
    };

    void addFile( const QString& file );
    void addDirectory( const QString& dir );

    QVector<ActivityFile> files();

    Type      _type;
    qlonglong _id;
    QString   _subject;
    QString   _message;
    QString   _file;
    QUrl      _link;
    QDateTime _dateTime;
    QString   _accName;

    QVector <ActivityLink> _links;
    /**
     * @brief Sort operator to sort the list youngest first.
     * @param val
     * @return
     */


    Identifier ident() const;

private:

    QVector<ActivityFile> _files;
};

bool operator==( const Activity& rhs, const Activity& lhs );
bool operator<( const Activity& rhs, const Activity& lhs );

/* ==================================================================== */
/**
 * @brief The ActivityList
 * @ingroup gui
 *
 * A QList based list of Activities
 */

/**
 * @brief The ActivityList
 * @ingroup gui
 *
 * A QList based list of Activities
 */
class ActivityList:public QList<Activity>
{
public:
    ActivityList();
    void setAccountState(AccountState *ast);
    AccountState* accountState();

private:
    AccountState *_ast;
};

}

#endif // ACTIVITYDATA_H
