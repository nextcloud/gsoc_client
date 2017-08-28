#ifndef PROVIDERWIDGET_H
#define PROVIDERWIDGET_H

#include <QWidget>
#include <QSize>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include "ui_providerwidget.h"
#include "owncloudprovidermodel.h"

namespace OCC {

class ProviderWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProviderWidget(QWidget *parent = 0);
    ~ProviderWidget();
    void updateProvider(const QListWidgetItem *item);

    enum DataRole {
        headerTextRole      = Qt::UserRole+100,
        subHeaderTextRole   = Qt::UserRole+101,
        imageRole           = Qt::UserRole+102,
        registrationRole    = Qt::UserRole+103,
        providerUrlRole     = Qt::UserRole+104,
        freeRole            = Qt::UserRole+105,
        countryRole         = Qt::UserRole+106
    };

public slots:
    void finishedImageLoading(QPixmap *image);
    void openRegistration();
    void openInformation();

private:
    Ui_ProviderWidget *ui;
    OwncloudProviderModel *_model;
    int Request;
    QString _registrationUrl;
    QString _providerUrl;
    QNetworkAccessManager *_nam;
};

}
#endif // PROVIDERWIDGET_H
