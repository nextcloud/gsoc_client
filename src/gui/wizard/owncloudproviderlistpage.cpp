#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <QStringListModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QListWidgetItem>
#include <QObject>

#include "owncloudproviderlistpage.h"
#include "ui_owncloudproviderlistpage.h"
#include "theme.h"
#include "config.h"
#include "wizard/owncloudwizardcommon.h"
#include "QProgressIndicator.h"
#include "ui_providerwidget.h"
#include "providerwidget.h"

namespace OCC
{
OwncloudProviderListPage::OwncloudProviderListPage(QWidget *parent) :
    QWizardPage(parent),
    ui(new Ui_OwncloudProviderListPage),
    countryModel(new QStringListModel(this)),
    _progressIndicator(new QProgressIndicator(this))
{
    ui->setupUi(this);
    setTitle(WizardCommon::titleTemplate().arg(tr("Hosting providers")));
    setSubTitle(WizardCommon::subTitleTemplate().arg(tr("find a provider to create a new account")));
    setupCustomization();
}

OwncloudProviderListPage::~OwncloudProviderListPage()
{
    delete countryModel;
}

void OwncloudProviderListPage::setupCustomization()
{
    ui->horizontalLayout->addWidget(_progressIndicator, 0, Qt::AlignLeft);
    this->setLayout(ui->verticalLayout);
    ui->listWidget->setAlternatingRowColors(true);
    startSpinner();
}

void OwncloudProviderListPage::initializePage() {
    loadProviders();
}

void OwncloudProviderListPage::startSpinner()
{
    //_ui.resultLayout->setEnabled(true);
    _progressIndicator->setVisible(true);
    _progressIndicator->startAnimation();
}

void OwncloudProviderListPage::stopSpinner()
{
    ui->bottomLabel->setText("");
    ui->topLabel->setText("");
    _progressIndicator->setVisible(false);
    _progressIndicator->stopAnimation();
}

void OwncloudProviderListPage::loadProviders()
{

    _nam = new QNetworkAccessManager(this);
    QObject::connect(_nam, SIGNAL(finished(QNetworkReply*)),
                     this,
                     SLOT(serviceRequestFinished(QNetworkReply*)));
    QUrl url(APPLICATION_PROVIDERS);
    _nam->get(QNetworkRequest(url));
    qDebug() << "Loading providers from" << url;

}

void OwncloudProviderListPage::serviceRequestFinished(QNetworkReply* reply)
{
    if(reply->error() == QNetworkReply::NoError) {
        QString strReply = (QString)reply->readAll();
        QJsonDocument jsonResponse = QJsonDocument::fromJson(strReply.toUtf8());

        QStringList countryList;
        if (!jsonResponse.isArray())
            return;
        foreach (const QJsonValue & value, jsonResponse.array()) {
            QJsonObject object = value.toObject();
            foreach(const QJsonValue & flag, object["flags"].toArray()) {
                countryList << flag.toString();
            }
            ProviderWidget *widget = new ProviderWidget(this);
            QListWidgetItem *witem = new QListWidgetItem();
            witem->setData(ProviderWidget::DataRole::headerTextRole, object["title"].toString());
            witem->setData(ProviderWidget::DataRole::subHeaderTextRole, object["specializes"].toArray()[0].toString());
            witem->setData(ProviderWidget::DataRole::registrationRole, object["registration"].toString());
            witem->setData(ProviderWidget::DataRole::providerUrlRole, object["url"].toString());
            witem->setData(ProviderWidget::DataRole::imageRole, object["imagename"].toString());
            ui->listWidget->addItem(witem);

            ui->listWidget->setItemWidget(witem, qobject_cast<QWidget*>(widget));
            widget->updateProvider(witem);
            witem->setSizeHint(QSize(ui->listWidget->sizeHint().width(), widget->sizeHint().height()));
        }
        countryList.removeDuplicates();
        countryList.sort();
        countryModel->setStringList(countryList);
        ui->country->setModel(countryModel);
        reply->deleteLater();
        stopSpinner();
    } else {
        ui->bottomLabel->setText(tr("Failed to fetch provider list."));
    }
}

int OwncloudProviderListPage::nextId()
{
    return -1;
}

}
