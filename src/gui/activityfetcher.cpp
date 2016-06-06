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

#include "activityfetcher.h"
#include "activitydata.h"
#include "account.h"
#include "accountstate.h"
#include "json.h"
#include "networkjobs.h"

namespace OCC {

ActivityFetcher::ActivityFetcher(QObject *parent) : QObject(parent)
{

}

void ActivityFetcher::slotFetch(AccountState* s)
{
    if( !(s && s->isConnected() )) {
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

    qDebug() << "Start fetching activities for " << s->account()->displayName();
    job->start();

}

void ActivityFetcher::slotActivitiesReceived(const QVariantMap& json, int statusCode)
{
    auto activities = json.value("ocs").toMap().value("data").toList();
    qDebug() << "*** activities" << activities;

    ActivityList list;
    AccountState* ai = qvariant_cast<AccountState*>(sender()->property("AccountStatePtr"));
    list.setAccountState( ai );

    foreach( auto activ, activities ) {
        auto json = activ.toMap();

        Activity a;
        a._accName  = ai->account()->displayName();
        a._id       = json.value("id").toLongLong();
        a._subject  = json.value("subject").toString();
        a._message  = json.value("message").toString();
        const QString f = json.value("file").toString();
        a.addFile(f);
        a._link     = json.value("link").toUrl();
        a._dateTime = json.value("date").toDateTime();
        list.append(a);
    }
    // activity app is not enabled, signalling.
    if( statusCode == 999 ) {
        emit accountWithoutActivityApp(ai);
    }

    emit newActivityList(list);
}

/* ==================================================================== */

ActivityFetcherV2::ActivityFetcherV2()
    : ActivityFetcher()
{

}

ActivityList ActivityFetcherV2::fetchFromDb( const QString& accountId )
{
    // TODO fetch from database
    ActivityList dbActivities;

    return dbActivities;
}

int ActivityFetcherV2::lastSeenId()
{
    int lastId = 0;

    return lastId;
}

void ActivityFetcherV2::slotFetch(AccountState* s)
{
    if( !(s && s->isConnected() )) {
        return;
    }

    JsonApiJob *job = new JsonApiJob(s->account(), QLatin1String("ocs/v2.php/apps/activity/api/v2/activity"), this);
    QObject::connect(job, SIGNAL(jsonReceived(QVariantMap, int)),
                     this, SLOT(slotActivitiesReceived(QVariantMap, int)));
    job->setProperty("AccountStatePtr", QVariant::fromValue<AccountState*>(s));

    QList< QPair<QString,QString> > params;

    int lastId = lastSeenId();
    if( lastId > 0 ) {
        params.append(qMakePair(QString::fromLatin1("since"),  QString::number(lastId)));
        job->addQueryParams(params);
    }
    qDebug() << "Start fetching V2 activities for " << s->account()->displayName();
    job->start();
}

#define QL1(X) QLatin1String(X)

bool ActivityFetcherV2::parseActionString( Activity *activity, const QString& subject, const QVariantList& params)
{
    // the action contains a string describing what happened
    bool re = true;

    if( subject == QL1("shared_user_self") ) {

    } else if( subject == QL1("reshared_user_by") ) {

    } else if( subject == QL1("shared_group_self") ) {

    } else if( subject == QL1("reshared_group_by") ) {

    } else if( subject == QL1("reshared_link_by") ) {

    } else if( subject == QL1("shared_user_self") ) {

    } else if( subject == QL1("created_self") ) {

    } else if( subject == QL1("created_by") ) {

    } else if( subject == QL1("created_public") ) {

    } else if( subject == QL1("changed_self") ) {

    } else if( subject == QL1("changed_by") ) {

    } else if( subject == QL1("deleted_self") ) {

    } else if( subject == QL1("deleted_by") ) {

    } else if( subject == QL1("restored_self") ) {

    } else if( subject == QL1("restored_by") ) {

    } else {
        // unknown action.
        re = false;
    }

    // parse the params
    foreach( QVariant v, params ) {
        QVariantMap vm = v.toMap();

        if( vm.contains("type") ) {
            const QString type = vm.value("type").toString();
            const QString val = vm.value("value").toString();

            if( type == QL1("collection") ) {
                QVariantList items = vm.value("value").toList();

                foreach( QVariant vFile, items ) {
                    QVariantMap vMap = vFile.toMap();
                    const QString fileType = vMap.value("type").toString();
                    const QString relFileName = vMap.value("value").toString();

                    if( fileType != QL1("file")) {
                        activity->addDirectory(relFileName);
                    } else {
                        activity->addFile(relFileName);
                    }
                }
            } else if( type == QL1("file")) {
                const QString relFileName = val;
                activity->addFile(relFileName);
            } else if( type == QL1("dir")) {
                const QString relFileName = val;
                activity->addDirectory(relFileName);
                // needs verification!
            } else if( type == QL1("username")) {
                const QString user = val;
            } else if( type == QL1("typeicon")) {
                const QString icon = val;
            } else {

            }
        }
    }
    return re;
}

void ActivityFetcherV2::slotActivitiesReceived(const QVariantMap& json, int statusCode)
{
    auto activities = json.value("ocs").toMap().value("data").toList();
    qDebug() << "*** activities" << activities;

    AccountState* ai = qvariant_cast<AccountState*>(sender()->property("AccountStatePtr"));
    ActivityList list;

    if( ai ) {
        list = fetchFromDb(ai->account()->id());
        list.setAccountState( ai );

        foreach( auto activ, activities ) {
            auto json = activ.toMap();

            Activity a;
            a._accName  = ai->account()->displayName();
            a._id       = json.value("activity_id").toLongLong();
            QString subject = json.value("subject").toString();
            QVariantList subjectParams = json.value("subjectparams").toList();
            bool knownAction = parseActionString( &a, subject, subjectParams );

            a._subject  = json.value("subject").toString();

            a._message  = json.value("message_prepared").toString();
            // a._file     = json.value("file").toString();
            // a._link     = json.value("link").toUrl();
            a._dateTime = json.value("datetime").toDateTime();
            list.append(a);
        }
        // activity app is not enabled, signalling.
        if( statusCode == 999 ) {
            emit accountWithoutActivityApp(ai);
        }
    }
    emit newActivityList(list);
}

}
