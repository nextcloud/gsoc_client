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
#include <QAbstractListModel>
#include <QWidget>
#include <QIcon>

#include "account.h"
#include "accountstate.h"
#include "accountmanager.h"
#include "folderman.h"
#include "accessmanager.h"
#include "activityitemdelegate.h"

#include "activitydata.h"
#include "activitylistmodel.h"

#define FETCH_ACTIVITIES_AMOUNT 1000

namespace OCC {

/* ==================================================================== */
ActivitySortProxyModel::ActivitySortProxyModel(QObject *parent)
    :QSortFilterProxyModel(parent)
{

}

bool ActivitySortProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    QVariant leftData = sourceModel()->data(left);
    QVariant rightData = sourceModel()->data(right);

    if (leftData.type() == QVariant::DateTime) {
        return leftData.toDateTime() < rightData.toDateTime();
    } else {
        qDebug() << "OOOOO " << endl;
    }
    return true;
}

/* ==================================================================== */

ActivityListModel::ActivityListModel(QWidget *parent)
    :QAbstractListModel(parent),
      _fetchEntriesAmount(FETCH_ACTIVITIES_AMOUNT)
{
}

QVariant ActivityListModel::data(const QModelIndex &index, int role) const
{
    Activity a;

    if (!index.isValid())
        return QVariant();

    a = findItem(index.row());

    AccountStatePtr ast = AccountManager::instance()->account(a._accName);
    QStringList list;

    switch (role) {
    case ActivityItemDelegate::PathRole:
        list = FolderMan::instance()->findFileInLocalFolders(a._file, ast->account());
        if( list.count() > 0 ) {
            return QVariant(list.at(0));
        }
        // File does not exist anymore? Let's try to open its path
        list = FolderMan::instance()->findFileInLocalFolders(QFileInfo(a._file).path(), ast->account());
        if( list.count() > 0 ) {
            return QVariant(list.at(0));
        }
        return QVariant();
        break;
    case ActivityItemDelegate::ActionIconRole:
        return QVariant(); // FIXME once the action can be quantified, display on Icon
        break;
    case ActivityItemDelegate::UserIconRole:
        return QIcon(QLatin1String(":/client/resources/account.png"));
        break;
    case Qt::ToolTipRole:
    case ActivityItemDelegate::ActionTextRole:
        return a._subject;
        break;
    case ActivityItemDelegate::LinkRole:
        return a._link;
        break;
    case ActivityItemDelegate::AccountRole:
        return a._accName;
        break;
    case ActivityItemDelegate::PointInTimeRole:
        return Utility::timeAgoInWords(a._dateTime);
        break;
    case ActivityItemDelegate::AccountConnectedRole:
        return (ast && ast->isConnected());
        break;
    default:
        return QVariant();

    }
    return QVariant();

}

int ActivityListModel::rowCount(const QModelIndex&) const
{
    int cnt = 0;

    foreach( ActivityList al, _activityLists) {
        cnt += al.count();
    }
    return cnt;
}

void ActivityListModel::startFetchJob(AccountState* ast)
{
    if( !ast->isConnected() || _currentlyFetching.contains(ast)) {
        return;
    }

    int activityListIndx = activityListIndxForAccountState(ast);
    ActivityList activityList = _activityLists.at(activityListIndx);

    // remove entries that might exist in this list.
    int startItem = 0;
    for( int i = 0; i < activityListIndx; i++ ) {
        ActivityList al = _activityLists.at(i);
        startItem += al.count();
    }

    beginRemoveRows(QModelIndex(), startItem, activityList.count() );
    activityList.clear();
    endRemoveRows();

    _activityLists[activityListIndx] = activityList;

    // start a new fetch job.
    JsonApiJob *job = new JsonApiJob(ast->account(), QLatin1String("ocs/v1.php/cloud/activity"), this);
    QObject::connect(job, SIGNAL(jsonReceived(QVariantMap, int)),
                     this, SLOT(slotActivitiesReceived(QVariantMap, int)));
    job->setProperty("AccountStatePtr", QVariant::fromValue<AccountState*>(ast));
    QList< QPair<QString,QString> > params;
    params.append(qMakePair(QString::fromLatin1("start"), QLatin1String("0")));
    params.append(qMakePair(QString::fromLatin1("count"), QString::number(_fetchEntriesAmount)));
    job->addQueryParams(params);

    _currentlyFetching.insert(ast);
    qDebug() << Q_FUNC_INFO << "Start fetching activities for " << ast->account()->displayName();
    job->start();
}

void ActivityListModel::slotActivitiesReceived(const QVariantMap& json, int statusCode)
{
    auto activities = json.value("ocs").toMap().value("data").toList();

    AccountState* ast = qvariant_cast<AccountState*>(sender()->property("AccountStatePtr"));

    _currentlyFetching.remove(ast);

    // Read the new entries into a temporary list
    ActivityList list;
    foreach( auto activity, activities ) {
        auto json = activity.toMap();

        Activity a;
        a._type = Activity::ActivityType;
        a._accName  = ast->account()->displayName();
        a._id       = json.value("id").toLongLong();
        a._subject  = json.value("subject").toString();
        a._message  = json.value("message").toString();
        a._file     = json.value("file").toString();
        a._link     = json.value("link").toUrl();
        a._dateTime = json.value("date").toDateTime();
        list.append(a);
    }

    emit activityJobStatusCode(ast, statusCode);

    addNewActivities(ast, list);
}


void ActivityListModel::addNewActivities(AccountState* ast, const ActivityList& newItemsList)
{
    int startItem = 0; // the start number of items to delete in the virtual overall list
    int activityListIndx = activityListIndxForAccountState(ast);
    Q_ASSERT(activityListIndx != -1);

    ActivityList accountList = _activityLists.at(activityListIndx);

    for( int i = 0; i < activityListIndx; i++ ) {
        ActivityList li = _activityLists.at(i);
        startItem += li.count();
    }

    // insert the new list
    beginInsertRows(QModelIndex(), startItem, newItemsList.count() );
    accountList.append(newItemsList);
    endInsertRows();

    _activityLists[activityListIndx] = accountList;
}

int ActivityListModel::activityListIndxForAccountState(AccountState *ast)
{
    int i;

    for( i = 0; i < _activityLists.count(); i++ ) {
        ActivityList li = _activityLists.at(i);
        if( li.accountState() == ast )
            return i;
    }
    // if the AccountState was not found yet, add it to the list
    if( i == _activityLists.count() ) {
        ActivityList li;
        li.setAccountState(ast);
        _activityLists.append(li);
    }
    return i;
}

void ActivityListModel::slotRefreshActivity(AccountState *ast)
{
    if(ast ) {
        qDebug() << "**** Refreshing Activity list for" << ast->account()->displayName();
        startFetchJob(ast);
    }
}

void ActivityListModel::slotRemoveAccount(AccountState *ast )
{
    int removeIndx = activityListIndxForAccountState(ast);

    int startRow = 0;
    for( int i = 0; i < removeIndx; i++) {
        ActivityList al = _activityLists.at(i);
        startRow += al.count();
    }

    beginRemoveRows(QModelIndex(), startRow, startRow+_activityLists.at(removeIndx).count());
    _activityLists.removeAt(removeIndx);
    endRemoveRows();
    _currentlyFetching.remove(ast);
}

// combine all activities into one big result list
ActivityList ActivityListModel::activityList()
{
    ActivityList all;
    int i;

    for( i = 0; i < _activityLists.count(); i++) {
        ActivityList al = _activityLists.at(i);
        all.append(al);
    }
    return all;
}

Activity ActivityListModel::findItem(int indx) const
{
    Activity a;

    foreach( ActivityList al, _activityLists ) {
        if( indx < al.count() ) {
            a = al.at(indx);
            break;
        }
        indx -= al.count();
    }

    return a;
}

}
