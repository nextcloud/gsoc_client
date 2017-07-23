#ifndef CLOUDPROVIDERMANAGER_H
#define CLOUDPROVIDERMANAGER_H

#include <QObject>
#include "dbus_types.h"
#include "folder.h"

using namespace OCC;

class CloudProviderWrapper;

class CloudProviderManager : public QObject
{
    Q_OBJECT
public:
    explicit CloudProviderManager(QObject *parent = nullptr);
    void getMenuModel();
    void registerSignals();

signals:
    void InterfacesAdded(const QDBusObjectPath &object, InterfaceMap &interfaces);

public slots:
    void slotFolderListChanged(const Folder::Map &folderMap);

private:
    QMap<QString, CloudProviderWrapper*> *_map;
};

#endif // CLOUDPROVIDERMANAGER_H
