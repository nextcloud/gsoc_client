#ifndef OWNCLOUDPROVIDERMODEL_H
#define OWNCLOUDPROVIDERMODEL_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkReply>
#include <QPixmap>


class OwncloudProviderModel : public QObject
{
    Q_OBJECT
public:
    explicit OwncloudProviderModel(QObject *parent = nullptr);
    void setJsonObject(QJsonObject object);
    QVariant toQVariant();
    QString providerName();
    QString providerDescription();
    QString providerUrl();
    QUrl providerLogo();
    QPixmap* providerLogoImage();
    QString registrationUrl();
    bool free();
    QJsonArray flags();
    void loadImage();


signals:
    void logoReady(QPixmap *image);

public slots:
    void finishedImageLoading(QNetworkReply* reply);

private:

    QNetworkAccessManager *_nam;
    QString _providerName;
    QString _providerDescription;
    QString _providerUrl;
    QString _providerLogo;
    QString _registrationUrl;
    QPixmap* _providerLogoImage;
    bool _free;
    QJsonArray _flags;
};

#endif // OWNCLOUDPROVIDERMODEL_H
