extern "C" {
    #include <glib.h>
    #include <gio.h>
    #include <cloudproviderexporter.h>
}
#include "cloudproviderwrapper.h"
#include "cloudprovidermanager.h"
#include "config.h"
#include "folderman.h"
#include "accountstate.h"
#include "account.h"

CloudProviderExporter *_cloudProvider;
GDBusConnection *_connection;

void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    Q_UNUSED(name);
    QString orgName = QString(APPLICATION_VENDOR).replace(" ", "").toLower();
    QString appName = QString(APPLICATION_SHORTNAME).replace(" ", "").toLower();
    QString busName = "org." + orgName + "." + appName;
    QString objectName = "/org/" + orgName + "/" + appName;

    CloudProviderManager *self = static_cast<CloudProviderManager*>(user_data);
    _cloudProvider = cloud_provider_exporter_new(connection, busName.toAscii().data(), objectName.toAscii().data());
    _connection = connection;
    self->registerSignals();
}

static void on_name_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    Q_UNUSED(connection);
    Q_UNUSED(name);
    Q_UNUSED(user_data);
}

static void on_name_lost (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    Q_UNUSED(connection);
    Q_UNUSED(name);
    Q_UNUSED(user_data);
    g_object_unref(_cloudProvider);
}

void CloudProviderManager::registerSignals()
{
    OCC::FolderMan *folderManager = OCC::FolderMan::instance();
    connect(folderManager, SIGNAL(folderListChanged(const Folder::Map &)), SLOT(slotFolderListChanged(const Folder::Map &)));
    slotFolderListChanged(folderManager->map());
}

CloudProviderManager::CloudProviderManager(QObject *parent) : QObject(parent)
{
    _map = new QMap<QString, CloudProviderWrapper*>();
    QString orgName = QString(APPLICATION_VENDOR).replace(" ", "").toLower();
    QString appName = QString(APPLICATION_SHORTNAME).replace(" ", "").toLower();
    QString busName = "org." + orgName + "." + appName;
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    busName.toAscii().data(),
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired, on_name_acquired, on_name_lost,
                    this, NULL);
}

void CloudProviderManager::slotFolderListChanged(const Folder::Map &folderMap)
{
    QMapIterator<QString, CloudProviderWrapper*> i(*_map);
    while (i.hasNext()) {
        i.next();
        if (!folderMap.contains(i.key())) {
            cloud_provider_exporter_remove_account (_cloudProvider, i.value()->accountExporter());
            _map->remove(i.key());
        }
    }

    Folder::MapIterator j(folderMap);
    while (j.hasNext()) {
        j.next();
        if (!_map->contains(j.key())) {
            CloudProviderWrapper *cpo = new CloudProviderWrapper(this, j.value(), _cloudProvider);
            _map->insert(j.key(), cpo);
        }
    }
    cloud_provider_exporter_export_objects (_cloudProvider);
}
