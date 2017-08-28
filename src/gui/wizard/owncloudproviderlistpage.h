#ifndef OWNCLOUDPROVIDERLISTPAGE_H
#define OWNCLOUDPROVIDERLISTPAGE_H

#include <QWizardPage>
#include <QNetworkReply>
#include <QStringListModel>
#include "ui_owncloudproviderlistpage.h"
#include "owncloudwizard.h"
#include "owncloudprovidermodel.h"

class QProgressIndicator;


namespace OCC {

class OwncloudProviderListPage : public QWizardPage
{
    Q_OBJECT
public:
    OwncloudProviderListPage(QWidget *parent);
    ~OwncloudProviderListPage();
    int nextId() const;
    bool isComplete() const;
    void initializePage();
public slots:
    void toggleFreePlans(bool state);
    void setCountry(QString current);
    void openRegistration(OwncloudProviderModel *model);

protected slots:
    void setupCustomization();
    void serviceRequestFinished(QNetworkReply* reply);
    void startSpinner();
    void stopSpinner();

private:
    void loadProviders();
    void filterProviders();

    OwncloudWizard* _ocWizard;
    Ui_OwncloudProviderListPage *ui;
    QStringListModel *countryModel;
    QProgressIndicator *_progressIndicator;
    QNetworkAccessManager *_nam;
    bool showFreeOnly;
    QString *showCountryOnly;
};

}
#endif // OWNCLOUDPROVIDERLISTPAGE_H
