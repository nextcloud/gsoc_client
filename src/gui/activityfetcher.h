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

#ifndef ACTIVITYFETCHER_H
#define ACTIVITYFETCHER_H

#include <QtCore>

#include "activitydata.h"
#include "accountstate.h"

/**
 * @brief The ActivityFetcher class
 *
 * Used to fetch the list of server acitivities from the server. Accesses
 * the old ocs based API.
 */

namespace OCC {

class ActivityFetcher : public QObject
{
    Q_OBJECT
public:
    explicit ActivityFetcher(QObject *parent = 0);

public slots:
    virtual void slotFetch(AccountState* s);

private slots:
    virtual void slotActivitiesReceived(const QVariantMap& json, int statusCode);

signals:
    void newActivityList( ActivityList list );
    void accountWithoutActivityApp(AccountState*);

};

/* ==================================================================== */

/**
 * @brief The ActivityFetcherV2 class
 *
 * To be used with the next version of the activity API. By now, it is
 * completely unused.
 */

class ActivityFetcherV2 : public ActivityFetcher
{
    Q_OBJECT
public:
    explicit ActivityFetcherV2();

public slots:
    virtual void slotFetch(AccountState* s);

private slots:
    virtual void slotActivitiesReceived(const QVariantMap& json, int statusCode);

private:
    bool parseActionString( Activity *activity, const QString& subject, const QVariantList& params);
    ActivityList fetchFromDb(const QString &accountId );
    int lastSeenId();

};

}

#endif // ACTIVITYFETCHER_H
