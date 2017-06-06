#ifndef PROVIDERWIDGET_H
#define PROVIDERWIDGET_H

#include <QWidget>
#include <QSize>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include "ui_providerwidget.h"

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
        providerUrlRole     = Qt::UserRole+104
    };

public slots:
    void finishedImageLoading(QNetworkReply*);
    void openRegistration();
    void openInformation();

private:
    Ui_ProviderWidget *ui;
    int Request;
    QString _registrationUrl;
    QString _providerUrl;
    QNetworkAccessManager *_nam;
};

#endif // PROVIDERWIDGET_H
