#include <QUrl>
#include <QDesktopServices>
#include <QDebug>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QNetworkReply>


#include "providerwidget.h"

ProviderWidget::ProviderWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui_ProviderWidget)
{
    ui->setupUi(this);
    this->setLayout(ui->horizontalLayout);
}

ProviderWidget::~ProviderWidget()
{
    //delete ui;
}

void ProviderWidget::updateProvider(const QListWidgetItem *item)
{
    QString headerText = qvariant_cast<QString>(item->data(DataRole::headerTextRole));
    ui->providerName->setText(headerText);

    QString description = qvariant_cast<QString>(item->data(DataRole::subHeaderTextRole));
    ui->providerDesc->setText(description);

    _registrationUrl = qvariant_cast<QString>(item->data(DataRole::registrationRole));
    if(_registrationUrl.isEmpty()) {
            ui->registerButton->hide();
    } else {
        QObject::connect(ui->registerButton, SIGNAL (clicked()),
                         this, SLOT (openRegistration()));
    }

    _providerUrl = qvariant_cast<QString>(item->data(DataRole::providerUrlRole));
    QObject::connect(ui->informationButton, SIGNAL (clicked()),
                             this, SLOT (openInformation()));

    QUrl url("https://bitgrid.net/~jus/provider1.png");
    _nam = new QNetworkAccessManager(this);
    QObject::connect(_nam, SIGNAL(finished(QNetworkReply*)),
                     this,  SLOT(finishedImageLoading(QNetworkReply*)));
    _nam->get(QNetworkRequest(url));
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
