#include <QVariant>
#include "owncloudprovidermodel.h"
#include "config.h"

OwncloudProviderModel::OwncloudProviderModel(QObject *parent) :
    QObject(parent),
    _nam(new QNetworkAccessManager(this)),
    _providerLogoImage(nullptr)
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

void OwncloudProviderModel::loadImage() {
    if(this->_providerLogoImage != nullptr) {
        emit logoReady(_providerLogoImage);
        return;
    }
    QObject::connect(_nam, SIGNAL(finished(QNetworkReply*)),
                     this,  SLOT(finishedImageLoading(QNetworkReply*)));
    _nam->get(QNetworkRequest(this->providerLogo()));
}

void OwncloudProviderModel::finishedImageLoading(QNetworkReply* reply)
{
    if(reply->error() == QNetworkReply::NoError) {
        QImage img;
        img.loadFromData(reply->readAll());
        _providerLogoImage = new QPixmap(QPixmap::fromImage(img));
        emit logoReady(_providerLogoImage);
    }

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

QPixmap* OwncloudProviderModel::providerLogoImage() {
    if(this->_providerLogoImage != nullptr) {
        return this->_providerLogoImage;
    }
    return nullptr;
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
