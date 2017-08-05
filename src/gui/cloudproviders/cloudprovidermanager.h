#ifndef CLOUDPROVIDERMANAGER_H
#define CLOUDPROVIDERMANAGER_H

#include <QObject>
#include "folder.h"

using namespace OCC;

class CloudProviderWrapper;

class CloudProviderManager : public QObject
{
    Q_OBJECT
public:
    explicit CloudProviderManager(QObject *parent = nullptr);
    void registerSignals();

signals:

public slots:
    void slotFolderListChanged(const Folder::Map &folderMap);

private:
    QMap<QString, CloudProviderWrapper*> *_map;
};

#endif // CLOUDPROVIDERMANAGER_H
