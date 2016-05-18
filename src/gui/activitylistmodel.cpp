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
    :QAbstractListModel(parent)
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

// current strategy: Fetch 100 items per Account
// ATTENTION: This method is const and thus it is not possible to modify
// the _activityLists hash or so. Doesn't make it easier...
bool ActivityListModel::canFetchMore(const QModelIndex& ) const
{
    // if there are no activitylists registered yet, always
    // let fetch more.
    if( _activityLists.size() == 0 &&
            AccountManager::instance()->accounts().count() > 0 ) {
        return true;
    }
    int readyToFetch = 0;
    foreach( ActivityList list, _activityLists) {
        AccountStatePtr ast = AccountManager::instance()->account(list.accountName());

        if( ast && ast->isConnected()
                && list.count() == 0
                && ! _currentlyFetching.contains(ast.data()) ) {
            readyToFetch++;
        }
    }

    return readyToFetch > 0;
}

void ActivityListModel::startFetchJob(AccountState* s)
{
    if( !s->isConnected() ) {
        return;
    }
    JsonApiJob *job = new JsonApiJob(s->account(), QLatin1String("ocs/v1.php/cloud/activity"), this);
    QObject::connect(job, SIGNAL(jsonReceived(QVariantMap, int)),
                     this, SLOT(slotActivitiesReceived(QVariantMap, int)));
    job->setProperty("AccountStatePtr", QVariant::fromValue<AccountState*>(s));

    QList< QPair<QString,QString> > params;
    params.append(qMakePair(QString::fromLatin1("page"),     QString::fromLatin1("0")));
    params.append(qMakePair(QString::fromLatin1("pagesize"), QString::fromLatin1("100")));
    job->addQueryParams(params);

    _currentlyFetching.insert(s);
    qDebug() << Q_FUNC_INFO << "Start fetching activities for " << s->account()->displayName();
    job->start();
}

void ActivityListModel::slotActivitiesReceived(const QVariantMap& json, int statusCode)
{
    auto activities = json.value("ocs").toMap().value("data").toList();

    ActivityList list;
    AccountState* ast = qvariant_cast<AccountState*>(sender()->property("AccountStatePtr"));
    _currentlyFetching.remove(ast);

    foreach( auto activ, activities ) {
        auto json = activ.toMap();

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

    addNewActivities(list);
}


void ActivityListModel::addNewActivities(const ActivityList& list)
{
    int startItem = 0; // the start number of items to delete in the virtual overall list
    int listIdx = 0;   // the index of the list that is to replace.

    // check the existing list of activity lists if the incoming account
    // is already in there.
    foreach( ActivityList oldList, _activityLists ) {
        if( oldList.accountName() == list.accountName() ) {
            // listIndx contains the array index of the list and startItem
            // the start item of the virtual overall list.
            break;
        }
        startItem += oldList.count(); // add the number of items in the list
        listIdx++;
    }

    // if the activity list for this account was already known, remove its
    // entry first and than insert the new list.
    if( listIdx < _activityLists.count()-1 ) {
        int removeItemCount = _activityLists.at(listIdx).count();
        beginRemoveRows(QModelIndex(), startItem, removeItemCount);
        _activityLists.value(listIdx).clear();
        endRemoveRows();
    }

    // insert the new list
    beginInsertRows(QModelIndex(), startItem, list.count() );
    if( listIdx == _activityLists.count() ) {
        // not yet in the list of activity lists
        _activityLists.append(list);
    } else {
        _activityLists[listIdx] = list;
    }
    endInsertRows();

}

void ActivityListModel::fetchMore(const QModelIndex &)
{
    QList<AccountStatePtr> accounts = AccountManager::instance()->accounts();

    foreach ( AccountStatePtr ast, accounts) {
        // For each account from the account manager, check if it has already
        // an entry in the models list, if not, add one and call the fetch
        // job.
        bool found = false;
        foreach (ActivityList list, _activityLists) {
            if( AccountManager::instance()->account(list.accountName())==ast.data()) {
                found = true;
                break;
            }
        }

        if( !found ) {
            // add new list to the activity lists
            ActivityList alist;
            alist.setAccountName(ast->account()->displayName());
            _activityLists.append(alist);
            startFetchJob(ast.data());

        }
    }
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
    int i;
    int removeIndx = -1;
    int startRow = 0;
    for( i = 0; i < _activityLists.count(); i++) {
        ActivityList al = _activityLists.at(i);
        if( al.accountName() == ast->account()->displayName() ) {
            removeIndx = i;
            break;
        }
        startRow += al.count();
    }

    if( removeIndx > -1 ) {
        beginRemoveRows(QModelIndex(), startRow, startRow+_activityLists.at(removeIndx).count());
        _activityLists.removeAt(removeIndx);
        endRemoveRows();
        _currentlyFetching.remove(ast);
    }
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
