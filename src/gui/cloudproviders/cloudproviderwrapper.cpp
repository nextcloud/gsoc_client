extern "C" {
#include <glib.h>
#include <gio.h>
#include <cloudprovider.h>
}

#include "cloudproviderwrapper.h"
#include <account.h>
#include <folder.h>
#include <accountstate.h>
#include <QDesktopServices>
#include "openfilemanager.h"
#include "owncloudgui.h"
#include "application.h"

using namespace OCC;

static gchar* qstring_to_gchar(QString string)
{
    return string.toUtf8().data();
}

static void
on_get_name (CloudProviderAccount1 *cloud_provider, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)(cloud_provider);
    CloudProviderWrapper *self = static_cast<CloudProviderWrapper*>(user_data);
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", self->name));
}

static void
on_get_icon (CloudProviderAccount1 *cloud_provider, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)(cloud_provider);
    CloudProviderWrapper *self = static_cast<CloudProviderWrapper*>(user_data);
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(v)", g_icon_serialize(G_ICON(self->icon))));
}

static void
on_get_path (CloudProviderAccount1 *cloud_provider, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)(cloud_provider);
    CloudProviderWrapper *self = static_cast<CloudProviderWrapper*>(user_data);
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", self->path));
}

static void
on_get_status (CloudProviderAccount1 *cloud_provider, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)(cloud_provider);
    CloudProviderWrapper *self = static_cast<CloudProviderWrapper*>(user_data);
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(i)", self->status));
}

static void
on_get_status_details (CloudProviderAccount1 *cloud_provider, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)(cloud_provider);
    CloudProviderWrapper *self = static_cast<CloudProviderWrapper*>(user_data);
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", self->statusText()));
}

CloudProviderWrapper::CloudProviderWrapper(QObject *parent, Folder *folder, CloudProvider* cloudprovider) : QObject(parent)
  , _folder(folder)
{
    _recentlyChanged = new QList<QString>();
    _statusText = QString("");
    this->path = g_strdup(qstring_to_gchar(folder->cleanPath()));
    this->icon = g_icon_new_for_string("Nextcloud", NULL);
    this->status = 1;
    this->name = g_strdup(qstring_to_gchar(folder->shortGuiLocalPath()));
    gchar *folderAlias = folder->alias().toUtf8().data();
    gchar *accountId = folder->accountState()->account()->id().toUtf8().data();
    _accountName = g_strdup_printf ("Account%sFolder%s", accountId, folderAlias);

    _cloudProvider= CLOUD_PROVIDER(cloudprovider);
    _cloudProviderAccount1 = cloud_provider_account1_skeleton_new();
    g_signal_connect(_cloudProviderAccount1, "handle_get_name", G_CALLBACK (on_get_name), this);
    g_signal_connect(_cloudProviderAccount1, "handle_get_icon", G_CALLBACK (on_get_icon), this);
    g_signal_connect(_cloudProviderAccount1, "handle_get_path", G_CALLBACK (on_get_path), this);
    g_signal_connect(_cloudProviderAccount1, "handle_get_status", G_CALLBACK (on_get_status), this);
    g_signal_connect(_cloudProviderAccount1, "handle_get_status_details", G_CALLBACK (on_get_status_details), this);


    cloud_provider_export_account(_cloudProvider, _accountName, _cloudProviderAccount1);
    cloud_provider_export_menu (_cloudProvider, _accountName, getMenuModel());
    cloud_provider_export_actions (_cloudProvider, _accountName, getActionGroup());


    ProgressDispatcher *pd = ProgressDispatcher::instance();
    connect(pd, SIGNAL(progressInfo(QString, ProgressInfo)), this, SLOT(slotUpdateProgress(QString, ProgressInfo)));

    connect(this, SIGNAL(CloudProviderChanged()), this, SLOT(slotCloudProviderChanged()));
    connect(_folder, SIGNAL(syncStarted()), this, SLOT(slotSyncStarted()));
    connect(_folder, SIGNAL(syncFinished(SyncResult)), this, SLOT(slotSyncFinished(const SyncResult)));

    //g_free(account_object_name);

    emit CloudProviderChanged();
}

gchar* CloudProviderWrapper::accountName()
{
    return _accountName;
}

static bool shouldShowInRecentsMenu(const SyncFileItem &item)
{
    return !Progress::isIgnoredKind(item._status)
        && item._instruction != CSYNC_INSTRUCTION_EVAL
        && item._instruction != CSYNC_INSTRUCTION_NONE;
}

void CloudProviderWrapper::slotUpdateProgress(const QString &folder, const ProgressInfo &progress)
{
    // Only update progress for the current folder
    Folder *f = FolderMan::instance()->folder(folder);
    if (f != _folder)
        return;

    // Build recently changed files list
    if (!progress._lastCompletedItem.isEmpty()
        && shouldShowInRecentsMenu(progress._lastCompletedItem)) {
        if (Progress::isWarningKind(progress._lastCompletedItem._status)) {
            // display a warn icon if warnings happened.
        }

        QString kindStr = Progress::asResultString(progress._lastCompletedItem);
        QString timeStr = QTime::currentTime().toString("hh:mm");
        QString actionText = tr("%1 (%2, %3)").arg(progress._lastCompletedItem._file, kindStr, timeStr);
        Folder *f = FolderMan::instance()->folder(folder);
        if (f) {
            QString fullPath = f->path() + '/' + progress._lastCompletedItem._file;
            if (QFile(fullPath).exists()) {
                // add action
            }
        }

        if (_recentlyChanged->length() > 5)
            _recentlyChanged->removeFirst();
        _recentlyChanged->append(actionText);

        // FIXME: rebuild menu
    }

    // Build status details text
    if (!progress._currentDiscoveredFolder.isEmpty()) {
        _statusText =  tr("Checking for changes in '%1'").arg(progress._currentDiscoveredFolder);
        emit CloudProviderChanged();
    } else if (progress.totalSize() == 0) {
        quint64 currentFile = progress.currentFile();
        quint64 totalFileCount = qMax(progress.totalFiles(), currentFile);
        QString msg;
        if (progress.trustEta()) {
            msg = tr("Syncing %1 of %2  (%3 left)")
                      .arg(currentFile)
                      .arg(totalFileCount)
                      .arg(Utility::durationToDescriptiveString2(progress.totalProgress().estimatedEta));
        } else {
            msg = tr("Syncing %1 of %2")
                      .arg(currentFile)
                      .arg(totalFileCount);
        }
        _statusText = msg;
        emit CloudProviderChanged();
    } else {
        QString totalSizeStr = Utility::octetsToString(progress.totalSize());
        QString msg;
        if (progress.trustEta()) {
            msg = tr("Syncing %1 (%2 left)")
                      .arg(totalSizeStr, Utility::durationToDescriptiveString2(progress.totalProgress().estimatedEta));
        } else {
            msg = tr("Syncing %1")
                      .arg(totalSizeStr);
        }
        msg = tr("Syncing %1 (%2 left)")
                  .arg(totalSizeStr, Utility::durationToDescriptiveString2(progress.totalProgress().estimatedEta));
        _statusText = msg;
        emit CloudProviderChanged();
    }

    if (progress.isUpdatingEstimates()
            && progress.completedFiles() >= progress.totalFiles()
            && progress._currentDiscoveredFolder.isEmpty()) {
            QTimer::singleShot(2000, this, SLOT(slotStatusUpdateIdle()));
        }
}

void CloudProviderWrapper::slotStatusUpdateIdle()
{
    _statusText = tr("Up to date");
    emit CloudProviderChanged();
}


gchar* CloudProviderWrapper::statusText()
{
    return _statusText.toUtf8().data();
}


Folder* CloudProviderWrapper::folder()
{
    return _folder;
}

void CloudProviderWrapper::slotCloudProviderChanged()
{
    cloud_provider_account1_emit_cloud_provider_changed(_cloudProviderAccount1);
}


void CloudProviderWrapper::slotSyncStarted()
{
    status = CLOUD_PROVIDER_STATUS_SYNCING;
    emit CloudProviderChanged();
}

void CloudProviderWrapper::slotSyncFinished(const SyncResult &result)
{
    if (result.status() == result.Success)
    {
        status = CLOUD_PROVIDER_STATUS_IDLE;
        emit CloudProviderChanged();
        return;
    }
    status = CLOUD_PROVIDER_STATUS_ERROR;
    emit CloudProviderChanged();
}

GMenuModel* CloudProviderWrapper::getMenuModel() {

    GMenu* section;
    GMenu* mainMenu;
    GMenuItem* item;

    mainMenu = g_menu_new();

    section = g_menu_new();
    //item = g_menu_item_new(qstring_to_gchar(_folder->syncResult().statusString()), NULL);
    //g_menu_append_item(section, item);
    item = g_menu_item_new("Open website", "cloudprovider.openwebsite");
    g_menu_append_item(section, item);
    g_menu_append_section(mainMenu, NULL, G_MENU_MODEL(section));

    // Recently changed files menu
    GMenu* recentMenu = g_menu_new();
    section = g_menu_new();

    // FIXME: use real data
    item = g_menu_item_new("file.pdf", "cloudprovider.openfolder");
    g_menu_append_item(recentMenu, item);
    item = g_menu_item_new("file.pdf", "cloudprovider.openfolder");
    g_menu_append_item(recentMenu, item);
    item = g_menu_item_new("file.pdf", "cloudprovider.openfolder");
    g_menu_append_item(recentMenu, item);
    item = g_menu_item_new("file.pdf", "cloudprovider.openfolder");
    g_menu_append_item(recentMenu, item);

    item = g_menu_item_new_submenu("Recently changed", G_MENU_MODEL(recentMenu));
    g_menu_append_item(section, item);
    g_menu_append_section(mainMenu, NULL, G_MENU_MODEL(section));

    // Additional items
    section = g_menu_new();
    item = g_menu_item_new("Pause synchronization", "cloudprovider.pause");
    g_menu_append_item(section, item);

    g_menu_append_section(mainMenu, NULL, G_MENU_MODEL(section));

    section = g_menu_new();
    item = g_menu_item_new("Help", "cloudprovider.openhelp");
    g_menu_append_item(section, item);
    item = g_menu_item_new("Settings", "cloudprovider.opensettings");
    g_menu_append_item(section, item);
    item = g_menu_item_new("Log out", "cloudprovider.logout");
    g_menu_append_item(section, item);
    item = g_menu_item_new("Quit sync client", "cloudprovider.quit");
    g_menu_append_item(section, item);
    g_menu_append_section(mainMenu, NULL, G_MENU_MODEL(section));

    return G_MENU_MODEL(mainMenu);
}

static void
activate_action_open (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)(parameter);
    const gchar *name = g_action_get_name(G_ACTION(action));
    CloudProviderWrapper *self = static_cast<CloudProviderWrapper*>(user_data);
    ownCloudGui *gui = qobject_cast<ownCloudGui*>(self->parent()->parent());

    if(g_str_equal(name, "openhelp")) {
        gui->slotHelp();
    }

    if(g_str_equal(name, "opensettings")) {
            gui->slotShowSettings();
        }

    if(g_str_equal(name, "openwebsite")) {
        QDesktopServices::openUrl(self->folder()->accountState()->account()->url());
    }

    if(g_str_equal(name, "openfolder")) {
        showInFileManager(self->folder()->cleanPath());
    }

    if(g_str_equal(name, "logout")) {
        self->folder()->accountState()->signOutByUi();
    }

    if(g_str_equal(name, "quit")) {
        qApp->quit();
    }
}

static void
activate_action_openrecentfile (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)(action);
    (void)(parameter);
    CloudProviderWrapper *self = static_cast<CloudProviderWrapper*>(user_data);
    QDesktopServices::openUrl(self->folder()->accountState()->account()->url());
}

static void
activate_action_pause (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  GVariant *old_state, *new_state;

  old_state = g_action_get_state (G_ACTION (action));
  new_state = g_variant_new_boolean (!g_variant_get_boolean (old_state));

  /*g_print ("Toggle action %s activated, state changes from %d to %d\n",
           g_action_get_name (G_ACTION (action)),
           g_variant_get_boolean (old_state),
           g_variant_get_boolean (new_state));*/

  g_simple_action_set_state (action, new_state);
  g_variant_unref (old_state);
}

static GActionEntry actions[] = {
    { "openwebsite",  activate_action_open, NULL, NULL, NULL, {0,0,0}},
    { "quit",  activate_action_open, NULL, NULL, NULL, {0,0,0}},
    { "logout",  activate_action_open, NULL, NULL, NULL, {0,0,0}},
    { "openfolder",  activate_action_open, NULL, NULL, NULL, {0,0,0}},
    { "openhelp",  activate_action_open, NULL, NULL, NULL, {0,0,0}},
    { "opensettings",  activate_action_open, NULL, NULL, NULL, {0,0,0}},
    { "openrecentfile",  activate_action_openrecentfile, "s", NULL, NULL, {0,0,0}},
    { "pause",  activate_action_pause, NULL, "false", NULL, {0,0,0}},

};

GActionGroup* CloudProviderWrapper::getActionGroup()
{
    GSimpleActionGroup *group;
    group = g_simple_action_group_new ();
    g_action_map_add_action_entries (G_ACTION_MAP (group),
                                     actions,
                                     G_N_ELEMENTS (actions), this);
    return G_ACTION_GROUP (group);
}
