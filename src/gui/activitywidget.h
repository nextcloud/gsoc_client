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

#ifndef ACTIVITYWIDGET_H
#define ACTIVITYWIDGET_H

#include <QDialog>
#include <QDateTime>
#include <QLocale>
#include <QAbstractListModel>

#include "progressdispatcher.h"
#include "owncloudgui.h"
#include "account.h"
#include "activitydata.h"

#include "ui_activitywidget.h"

class QPushButton;
class QProgressIndicator;

namespace OCC {

class Account;
class AccountStatusPtr;
class ProtocolWidget;
class IssuesWidget;
class JsonApiJob;
class NotificationWidget;
class ActivityListModel;

namespace Ui {
    class ActivityWidget;
}
class Application;

/**
 * @brief The ActivityWidget class
 * @ingroup gui
 *
 * The list widget to display the activities, contained in the
 * subsequent ActivitySettings widget.
 */

class ActivityWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityWidget(QWidget *parent = 0);
    ~ActivityWidget();
    QSize sizeHint() const Q_DECL_OVERRIDE { return ownCloudGui::settingsDialogSize(); }
    void storeActivityList(QTextStream &ts);

    /**
     * Adjusts the activity tab's and some widgets' visibility
     *
     * Based on whether activities are enabled and whether notifications are
     * available.
     */
    void checkActivityTabVisibility();

public slots:
    void slotOpenFile(QModelIndex indx);
    void slotRefreshActivities(AccountState *ptr);
    void slotRefreshNotifications(AccountState *ptr);
    void slotRemoveAccount(AccountState *ptr);
    void slotAccountActivityStatus(AccountState *ast, int statusCode);
    void slotRequestCleanupAndBlacklist(const Activity &blacklistActivity);

signals:
    void guiLog(const QString &, const QString &);
    void copyToClipboard();
    void rowsInserted();
    void hideActivityTab(bool);
    void newNotification();

private slots:
    void slotBuildNotificationDisplay(const ActivityList &list);
    void slotSendNotificationRequest(const QString &accountName, const QString &link, const QByteArray &verb);
    void slotNotifyNetworkError(QNetworkReply *);
    void slotNotifyServerFinished(const QString &reply, int replyCode);
    void endNotificationRequest(NotificationWidget *widget, int replyCode);
    void scheduleWidgetToRemove(NotificationWidget *widget, int milliseconds = 4500);
    void slotCheckToCleanWidgets();

private:
    void showLabels();
    QString timeString(QDateTime dt, QLocale::FormatType format) const;
    Ui::ActivityWidget *_ui;
    QPushButton *_copyBtn;

    QSet<QString> _accountsWithoutActivities;
    QMap<Activity::Identifier, NotificationWidget *> _widgetForNotifId;
    QElapsedTimer _guiLogTimer;
    QSet<int> _guiLoggedNotifications;
    ActivityList _blacklistedNotifications;

    QHash<NotificationWidget *, QDateTime> _widgetsToRemove;
    QTimer _removeTimer;

    // number of currently running notification requests. If non zero,
    // no query for notifications is started.
    int _notificationRequestsRunning;

    ActivityListModel *_model;
    QVBoxLayout *_notificationsLayout;
};


/**
 * @brief The ActivitySettings class
 * @ingroup gui
 *
 * Implements a tab for the settings dialog, displaying the three activity
 * lists.
 */
class ActivitySettings : public QWidget
{
    Q_OBJECT
public:
    explicit ActivitySettings(QWidget *parent = 0);
    ~ActivitySettings();
    QSize sizeHint() const Q_DECL_OVERRIDE { return ownCloudGui::settingsDialogSize(); }

public slots:
    void slotRefresh(AccountState *ptr);
    void slotRemoveAccount(AccountState *ptr);

    void setNotificationRefreshInterval(quint64 interval);

    void slotShowIssuesTab(const QString &folderAlias);

private slots:
    void slotCopyToClipboard();
    void setActivityTabHidden(bool hidden);
    void slotRegularNotificationCheck();
    void slotShowIssueItemCount(int cnt);
    void slotShowActivityTab();

signals:
    void guiLog(const QString &, const QString &);

private:
    bool event(QEvent *e) Q_DECL_OVERRIDE;

    QTabWidget *_tab;
    int _activityTabId;
    int _protocolTabId;
    int _syncIssueTabId;

    ActivityWidget *_activityWidget;
    ProtocolWidget *_protocolWidget;
    IssuesWidget *_issuesWidget;
    QProgressIndicator *_progressIndicator;
    QTimer _notificationCheckTimer;
    QHash<AccountState *, QElapsedTimer> _timeSinceLastCheck;
};
}
#endif // ActivityWIDGET_H
