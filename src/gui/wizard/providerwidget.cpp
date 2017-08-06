#include <QUrl>
#include <QDesktopServices>
#include <QDebug>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QNetworkReply>
#include <QVariant>

#include "config.h"
#include "providerwidget.h"
#include "owncloudprovidermodel.h"

ProviderWidget::ProviderWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui_ProviderWidget)
{
    ui->setupUi(this);
}

ProviderWidget::~ProviderWidget()
{
    //delete ui;
}

void ProviderWidget::updateProvider(const QListWidgetItem *item)
{
    OwncloudProviderModel *model = item->data(Qt::UserRole).value<OwncloudProviderModel*>();
    ui->providerName->setText(model->providerName());
    ui->providerDesc->setText(model->providerDescription());
    if(!model->free()) {
        ui->labelFree->setVisible(false);
    }

    _registrationUrl = model->registrationUrl();
    if(_registrationUrl.isEmpty()) {
            ui->registerButton->hide();
    } else {
        QObject::connect(ui->registerButton, SIGNAL (clicked()),
                         this, SLOT (openRegistration()));
    }

    _providerUrl = model->providerUrl();

    QObject::connect(ui->informationButton, SIGNAL (clicked()),
                             this, SLOT (openInformation()));
    QObject::connect(ui->providerLogo, SIGNAL (clicked()),
                                this, SLOT (openInformation()));

    _nam = new QNetworkAccessManager(this);
    QObject::connect(_nam, SIGNAL(finished(QNetworkReply*)),
                     this,  SLOT(finishedImageLoading(QNetworkReply*)));
    _nam->get(QNetworkRequest(model->providerLogo()));

    updateGeometry();
}

void ProviderWidget::openRegistration()
{
    qDebug() << "Open registration for " << _registrationUrl;
}

void ProviderWidget::openInformation()
{
    QUrl url = QUrl(_providerUrl);
    QDesktopServices::openUrl(url);
}

void ProviderWidget::finishedImageLoading(QNetworkReply* reply)
{
    if(reply->error() == QNetworkReply::NoError) {
        QImage img;
        img.loadFromData(reply->readAll());
        ui->providerLogo->setPixmap(QPixmap::fromImage(img));
        ui->providerLogo->setScaledContents(true);
    }
}
