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

#ifndef ACTIVITYLISTMODEL_H
#define ACTIVITYLISTMODEL_H

#include <QtCore>

#include "activitydata.h"
#include "accountstate.h"

namespace OCC {

class ActivitySortProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    ActivitySortProxyModel(QObject *parent = 0);

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const Q_DECL_OVERRIDE;

};

/**
 * @brief The ActivityListModel
 * @ingroup gui
 *
 * Simple list model to provide the list view with data.
 */

class ActivityListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit ActivityListModel(QWidget *parent=0);

    QVariant data(const QModelIndex &index, int role) const Q_DECL_OVERRIDE;
    int rowCount(const QModelIndex& parent = QModelIndex()) const Q_DECL_OVERRIDE;

    ActivityList activityList();
    Activity findItem(int indx) const;

public slots:
    void slotRefreshActivity(AccountState* ast);
    void slotRemoveAccount( AccountState *ast );

private slots:
    void slotActivitiesReceived(const QVariantMap& json, int statusCode);

signals:
    void activityJobStatusCode(AccountState* ast, int statusCode);

private:
    void addNewActivities(AccountState* ast, const ActivityList& newItemsList);
    void startFetchJob(AccountState *ast);
    void combineActivityLists();
    int activityListIndxForAccountState(AccountState *ast );

    QList<ActivityList> _activityLists;
    ActivityList _finalList;
    QSet<AccountState*> _currentlyFetching;
    int _fetchEntriesAmount;
};

}
#endif // ACTIVITYLISTMODEL_H
