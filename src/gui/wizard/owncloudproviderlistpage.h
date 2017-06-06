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

    Ui_OwncloudProviderListPage *ui;
    QNetworkAccessManager *_nam;
    QStringListModel *countryModel;
    QProgressIndicator *_progressIndicator;
};

}
#endif // OWNCLOUDPROVIDERLISTPAGE_H
