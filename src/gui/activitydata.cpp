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

#include <QtCore>

#include "activitydata.h"

namespace OCC
{

ActivityFile::ActivityFile()
    :_type(Unknown)
{

}

ActivityFile::ActivityFile( const QString& file )
    :_relFileName(file),
      _type(File)
{

}

void ActivityFile::setType( FileType type )
{
    _type = type;
}

QString ActivityFile::relativePath() const
{
    return _relFileName;
}

QString ActivityFile::fullPath( const QString _accountName ) const
{
    QString fullPath(_relFileName);
    // FIXME: get the account and prepend the base path.

    if( _type == Directory && !fullPath.endsWith('/')) {
        fullPath.append('/');
    }
    return fullPath;
}

/* ==================================================================== */

bool operator<( const Activity& rhs, const Activity& lhs ) {
    return rhs._dateTime.toMSecsSinceEpoch() > lhs._dateTime.toMSecsSinceEpoch();
}

bool operator==( const Activity& rhs, const Activity& lhs ) {
    return (rhs._type == lhs._type && rhs._id== lhs._id && rhs._accName == lhs._accName);
}

Activity::Identifier Activity::ident() const {
    return Identifier( _id, _accName );
}

void Activity::addFile( const QString& file )
{
    ActivityFile f(file);
    _files.append(f);
}

void Activity::addDirectory( const QString& dir )
{
    ActivityFile f(dir);
    f.setType(ActivityFile::Directory);
    _files.append(f);
}

QVector<ActivityFile> Activity::files()
{
    return _files;
}


/* ==================================================================== */

ActivityList::ActivityList()
{
}

void ActivityList::setAccountState(AccountState *ast)
{
    _ast = ast;
}

AccountState* ActivityList::accountState()
{
    return _ast;
}

}
