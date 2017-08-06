#ifndef OWNCLOUDPROVIDERMODEL_H
#define OWNCLOUDPROVIDERMODEL_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

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
    QString registrationUrl();
    bool free();
    QJsonArray flags();

signals:

public slots:

private:
    QString _providerName;
    QString _providerDescription;
    QString _providerUrl;
    QString _providerLogo;
    QString _registrationUrl;
    bool _free;
    QJsonArray _flags;
};

#endif // OWNCLOUDPROVIDERMODEL_H
