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
#include <QLocale>

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
    _progressIndicator(new QProgressIndicator(this)),
    showFreeOnly(true)
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
    ui->listWidget->setAlternatingRowColors(false);
    startSpinner();
}

void OwncloudProviderListPage::initializePage() {
    loadProviders();
    filterProviders();
}

void OwncloudProviderListPage::startSpinner()
{
    _progressIndicator->setVisible(true);
    _progressIndicator->startAnimation();
    ui->country->setVisible(false);
}

void OwncloudProviderListPage::stopSpinner()
{
    ui->bottomLabel->setText("");
    ui->country->setVisible(true);
    _progressIndicator->setVisible(false);
    _progressIndicator->stopAnimation();
}
void OwncloudProviderListPage::toggleFreePlans(bool state)
{
    showFreeOnly = state;
    filterProviders();
}

void OwncloudProviderListPage::setCountry(QString current)
{
    showCountryOnly = new QString(current);
    filterProviders();
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

void OwncloudProviderListPage::filterProviders()
{
    const int itemCount = ui->listWidget->count();
    for ( int index = 0; index < itemCount; index++)
    {
          QListWidgetItem *item = ui->listWidget->item(index);
          OwncloudProviderModel *provider = qvariant_cast<OwncloudProviderModel*>(item->data(Qt::UserRole));
          QJsonArray countries = provider->flags();
          bool free = provider->free();
          bool countryMatches = false;
          foreach (const QJsonValue & value, countries) {
              QString country = value.toString();
              if(country.contains(ui->country->currentData().toString())) {
                  countryMatches = true;
              }
          }

          if((showFreeOnly && !free) || !countryMatches) {
              item->setHidden(true);
          } else {
              item->setHidden(false);
          }
    }
}

void OwncloudProviderListPage::serviceRequestFinished(QNetworkReply* reply)
{
    QString currentCountry = QLocale::system().name().split('_').at(1).toLower();
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
            OwncloudProviderModel *model = new OwncloudProviderModel(this);
            model->setJsonObject(object);
            ProviderWidget *widget = new ProviderWidget(this);
            QListWidgetItem *witem = new QListWidgetItem();
            QVariant v = QVariant::fromValue(model);
            witem->setData(Qt::UserRole, v);
            widget->updateProvider(witem);
            widget->setMinimumWidth(ui->listWidget->width()-20);
            witem->setSizeHint(QSize(ui->listWidget->width()-20, widget->sizeHint().height()));
            ui->listWidget->addItem(witem);
            ui->listWidget->setItemWidget(witem, qobject_cast<QWidget*>(widget));
        }

        // Build country list
        countryList.removeDuplicates();
        ui->country->addItem(tr("All countries"), QVariant("all"));
        QList<QLocale> cnt = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);
        std::sort(cnt.begin(), cnt.end(), [ ]( const QLocale& l1, const QLocale& l2 )
        {
            return QLocale::countryToString(l1.country()) < QLocale::countryToString(l2.country());
        });
        foreach (const QLocale countryCode, cnt) {
            QLocale::Country country = countryCode.country();
            if(countryCode.name() == "C")
                continue;

            QString countryIdentifier = countryCode.name().split('_').at(1).toLower();
            if(countryList.contains(countryIdentifier) && ui->country->findData(QVariant(countryIdentifier)) == -1) {
                ui->country->addItem(QLocale::countryToString(country), QVariant(countryIdentifier));
            }
        }

        reply->deleteLater();
        stopSpinner();
    } else {
        ui->bottomLabel->setText(tr("Failed to fetch provider list."));
    }



    int index = ui->country->findData(QVariant(currentCountry));
    if ( index != -1 ) {
       ui->country->setCurrentIndex(index);
    } else {
        ui->country->setCurrentIndex(ui->country->findData(QVariant("all")));
    }
}

int OwncloudProviderListPage::nextId()
{
    return -1;
}

}
