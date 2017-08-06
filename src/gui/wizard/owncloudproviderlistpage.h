#ifndef OWNCLOUDPROVIDERLISTPAGE_H
#define OWNCLOUDPROVIDERLISTPAGE_H

#include <QWizardPage>
#include <QNetworkReply>
#include <QStringListModel>
#include "ui_owncloudproviderlistpage.h"

class QProgressIndicator;


namespace OCC {

class OwncloudProviderListPage : public QWizardPage
{
    Q_OBJECT
public:
    OwncloudProviderListPage(QWidget *parent);
    ~OwncloudProviderListPage();
    int nextId();
    void initializePage();
public slots:
    void toggleFreePlans(bool state);
    void setCountry(QString current);

protected slots:
    void setupCustomization();
    void serviceRequestFinished(QNetworkReply* reply);
    void startSpinner();
    void stopSpinner();
#ifdef APPLICATION_SERVERSETUP
    void openSetupInstructions();
#endif

private:
    void loadProviders();
    void filterProviders();

    Ui_OwncloudProviderListPage *ui;
    QNetworkAccessManager *_nam;
    QStringListModel *countryModel;
    QProgressIndicator *_progressIndicator;
    bool showFreeOnly;
    QString *showCountryOnly;
};

}
#endif // OWNCLOUDPROVIDERLISTPAGE_H
