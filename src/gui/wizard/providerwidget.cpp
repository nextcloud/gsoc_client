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
#include "providerwidget.h"
#include "owncloudproviderlistpage.h"

namespace OCC {

ProviderWidget::ProviderWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui_ProviderWidget)
{
    ui->setupUi(this);
}

ProviderWidget::~ProviderWidget()
{
    delete ui;
}

void ProviderWidget::updateProvider(const QListWidgetItem *item)
{
    OwncloudProviderModel *model = item->data(Qt::UserRole).value<OwncloudProviderModel*>();
    _model = model;
    ui->providerName->setText(model->providerName());
    ui->providerDesc->setText(model->providerDescription());
    if(!model->free()) {
        ui->labelFree->setVisible(false);
    }

    _registrationUrl = model->registrationUrl();
    if(_registrationUrl.isEmpty()) {
            ui->registerButton->hide();
            QObject::connect(ui->informationButton, SIGNAL (clicked()),
                                     this, SLOT (openInformation()));
    } else {
        ui->informationButton->hide();
        QObject::connect(ui->registerButton, SIGNAL (clicked()),
                         this, SLOT (openRegistration()));
    }

    _providerUrl = model->providerUrl();

    _nam = new QNetworkAccessManager(this);
    QObject::connect(_nam, SIGNAL(finished(QNetworkReply*)),
                     this,  SLOT(finishedImageLoading(QNetworkReply*)));
    _nam->get(QNetworkRequest(model->providerLogo()));

    QObject::connect(model, SIGNAL(logoReady(QPixmap *)), this, SLOT(finishedImageLoading(QPixmap *)));
    model->loadImage();

}

void ProviderWidget::openRegistration()
{
    qDebug() << "Open registration for " << _registrationUrl;
    OCC::OwncloudProviderListPage* page = qobject_cast<OCC::OwncloudProviderListPage*>(parentWidget()->parentWidget()->parentWidget());
    page->openRegistration(_model);
}

void ProviderWidget::openInformation()
{
    QUrl url = QUrl(_providerUrl);
    QDesktopServices::openUrl(url);
}

void ProviderWidget::finishedImageLoading(QPixmap *image)
{
    if(image != nullptr) {
        ui->providerLogo->setPixmap(*image);
        ui->providerLogo->setScaledContents(true);
    }
}

}
