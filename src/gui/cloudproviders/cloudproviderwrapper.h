#ifndef CLOUDPROVIDER_H
#define CLOUDPROVIDER_H

#include <QObject>
#include "folderman.h"

// Forward declaration required since gio header files interfere with QObject headers
struct _CloudProvider;
typedef _CloudProvider CloudProvider;
struct _CloudProviderAccount1;
typedef _CloudProviderAccount1 CloudProviderAccount1;
struct _GMenuModel;
typedef _GMenuModel GMenuModel;
struct _GMenu;
typedef _GMenu GMenu;
struct _GActionGroup;
typedef _GActionGroup GActionGroup;
typedef char gchar;
typedef void* gpointer;
typedef unsigned int guint;

using namespace OCC;

class CloudProviderWrapper : public QObject
{
    Q_OBJECT
public:
    explicit CloudProviderWrapper(QObject *parent = nullptr, OCC::Folder *folder = nullptr, CloudProvider* cloudprovider = nullptr);
    ~CloudProviderWrapper();
    gchar* accountName();
    OCC::Folder* folder();
    GMenuModel* getMenuModel();
    GActionGroup* getActionGroup();
    gpointer name;
    uint status;
    gpointer path;
    gpointer icon;
    gchar* statusText();
signals:
    void CloudProviderChanged();

public slots:
    void slotCloudProviderChanged();
    void slotSyncStarted();
    void slotSyncFinished(const SyncResult &);
    void slotUpdateProgress(const QString &folder, const ProgressInfo &progress);
    void slotStatusUpdateIdle();
    void slotSyncPausedChanged(Folder*, bool);

private:
    OCC::Folder *_folder;
    CloudProvider *_cloudProvider;
    gchar *_accountName;
    CloudProviderAccount1 *_cloudProviderAccount1;
    uint _syncStatus;
    QList<QPair<QString, QString>> *_recentlyChanged;
    QString _statusText;
    bool paused;
    GMenuModel* _menuModel;
    GMenu* mainMenu = NULL;
    GMenu* recentMenu = NULL;
};

#endif // CLOUDPROVIDER_H
