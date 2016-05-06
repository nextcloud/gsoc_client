/*
 * Copyright (C) by Cédric Bellegarde <gnumdk@gmail.com>
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

#ifdef HAVE_LIBSNORE
#include <libsnore/application.h>
#include <libsnore/notification/icon.h>
#endif

#include "systray.h"
#include "theme.h"
#include <QDebug>
#include <QPointer>

#ifdef USE_FDO_NOTIFICATIONS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#define NOTIFICATIONS_SERVICE "org.freedesktop.Notifications"
#define NOTIFICATIONS_PATH "/org/freedesktop/Notifications"
#define NOTIFICATIONS_IFACE "org.freedesktop.Notifications"
#endif

namespace OCC {

Systray::Systray( )
{
#ifdef HAVE_LIBSNORE
    Snore::SnoreCore &snore = Snore::SnoreCore::instance();
    snore.loadPlugins( Snore::SnorePlugin::Backend | Snore::SnorePlugin::SecondaryBackend );
    snore.setDefaultSettingsValue("Silent", true, Snore::LocalSetting );

    _application = Snore::Application( Theme::instance()->appName(), Theme::instance()->applicationIcon() );
    _application.hints().setValue( "use-markup", true );
    _application.hints().setValue( "windows-app-id",  Theme::instance()->appName() );
    _application.hints().setValue( "desktop-entry",  Theme::instance()->appNameGUI() );
    _application.hints().setValue( "tray-icon",  qVariantFromValue(QPointer<QSystemTrayIcon>(this)));

    // register the Alerts.
    Snore::Alert alert( Theme::instance()->appNameGUI(), QIcon() );
    _alerts.insert( QSystemTrayIcon::NoIcon, alert);
    _application.addAlert(alert);
    Snore::Alert alert2( Theme::instance()->appNameGUI(), Theme::instance()->syncStateIcon(SyncResult::Success, false ));
    _alerts.insert( QSystemTrayIcon::Information, alert2);
    _application.addAlert(alert2);
    Snore::Alert alert3( Theme::instance()->appNameGUI(), Theme::instance()->syncStateIcon(SyncResult::Problem, false ));
    _alerts.insert( QSystemTrayIcon::Warning, alert3);
    _application.addAlert(alert3);
    Snore::Alert alert4( Theme::instance()->appNameGUI(), Theme::instance()->syncStateIcon(SyncResult::Error, false ));
    _alerts.insert( QSystemTrayIcon::Critical, alert4);
    _application.addAlert(alert4);

    snore.registerApplication( _application );
    snore.setDefaultApplication( _application );

    // connect( &snore, SIGNAL( actionInvoked( Snore::Notification ) ), this, SLOT( slotActionInvoked( Snore::Notification ) ) );
#endif
}

void Systray::showMessage(const QString & title, const QString & message, MessageIcon icon,
                          int millisecondsTimeoutHint)
{
#ifdef HAVE_LIBSNORE
    Snore::Alert a = _alerts[NoIcon];
    if( _alerts.contains(icon) ) {
        a = _alerts[icon];
    }

    Snore::Notification n( _application , a, title, message, a.icon() );
    Snore::SnoreCore::instance().broadcastNotification( n );

    return;
#endif

#ifdef USE_FDO_NOTIFICATIONS
    if(QDBusInterface(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_IFACE).isValid()) {
        QList<QVariant> args = QList<QVariant>() << "owncloud" << quint32(0) << "owncloud"
                                                 << title << message << QStringList () << QVariantMap() << qint32(-1);
        QDBusMessage method = QDBusMessage::createMethodCall(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_IFACE, "Notify");
        method.setArguments(args);
        QDBusConnection::sessionBus().asyncCall(method);
    } else
#endif
#ifdef Q_OS_OSX
    if (canOsXSendUserNotification()) {
        sendOsXUserNotification(title, message);
    } else
#endif
    {
        QSystemTrayIcon::showMessage(title, message, icon, millisecondsTimeoutHint);
    }
}

void Systray::setToolTip(const QString &tip)
{
    QSystemTrayIcon::setToolTip(tr("%1: %2").arg(Theme::instance()->appNameGUI(), tip));
}

} // namespace OCC
