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
struct _GActionGroup;
typedef _GActionGroup GActionGroup;
typedef char gchar;
typedef void* gpointer;

using namespace OCC;

class CloudProviderWrapper : public QObject
{
    Q_OBJECT
public:
    explicit CloudProviderWrapper(QObject *parent = nullptr, OCC::Folder *folder = nullptr, CloudProvider* cloudprovider = nullptr);
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
private:
    OCC::Folder *_folder;
    CloudProvider *_cloudProvider;
    gchar *_accountName;
    CloudProviderAccount1 *_cloudProviderAccount1;
    uint _syncStatus;
    QList<QString> *_recentlyChanged;
    QString _statusText;
};

#endif // CLOUDPROVIDER_H
