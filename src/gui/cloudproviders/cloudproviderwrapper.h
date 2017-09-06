#ifndef CLOUDPROVIDER_H
#define CLOUDPROVIDER_H

#include <QObject>
#include "folderman.h"

/* Forward declaration required since gio header files interfere with QObject headers */
struct _CloudProviderExporter;
typedef _CloudProviderExporter CloudProviderExporter;
struct _CloudProviderAccountExporter;
typedef _CloudProviderAccountExporter CloudProviderAccountExporter;
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
    explicit CloudProviderWrapper(QObject *parent = nullptr, Folder *folder = nullptr, CloudProviderExporter* cloudprovider = nullptr);
    ~CloudProviderWrapper();
    gchar* accountName();
    CloudProviderAccountExporter* accountExporter();
    Folder* folder();
    GMenuModel* getMenuModel();
    GActionGroup* getActionGroup();
    gchar* name;
    uint status;
    gchar* path;
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
    Folder *_folder;
    CloudProviderExporter *_cloudProvider;
    gchar *_accountName;
    CloudProviderAccountExporter *_cloudProviderAccount1;
    uint _syncStatus;
    QList<QPair<QString, QString>> *_recentlyChanged;
    QString _statusText;
    bool paused;
    GMenuModel* _menuModel;
    GMenu* mainMenu = NULL;
    GMenu* recentMenu = NULL;
};

#endif // CLOUDPROVIDER_H
