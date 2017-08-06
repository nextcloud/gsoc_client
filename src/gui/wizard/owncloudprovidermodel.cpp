#include <QVariant>
#include "owncloudprovidermodel.h"
#include "config.h"

OwncloudProviderModel::OwncloudProviderModel(QObject *parent) : QObject(parent)
{

}

void OwncloudProviderModel::setJsonObject(QJsonObject object)
{
    _providerName = object["title"].toString();
    _providerDescription = object["specializes"].toArray()[0].toString();
    _providerUrl = object["url"].toString();
    _providerLogo = object["imagename"].toString();
    _registrationUrl = object["registration"].toString();
    _free = object["freeplans"].toBool();
    _flags = object["flags"].toArray();
}

QString OwncloudProviderModel::providerName()
{
    return _providerName;
}

QString OwncloudProviderModel::providerDescription()
{
    return _providerDescription;
}

QString OwncloudProviderModel::providerUrl()
{
    return _providerUrl;
}

QUrl OwncloudProviderModel::providerLogo() {
    QUrl url(_providerLogo);
    if(url.isRelative()) {
        QUrl baseUrl(APPLICATION_PROVIDERS);
        QUrl adjusted = baseUrl.adjusted(QUrl::UrlFormattingOption::RemoveFilename);
        url = QUrl(adjusted.toString() + _providerLogo);
    }
    return url;
}

QString OwncloudProviderModel::registrationUrl()
{
    return _registrationUrl;
}

bool OwncloudProviderModel::free()
{
    return _free;
}

QJsonArray OwncloudProviderModel::flags()
{
    return _flags;
}
