#include "owncloudproviderregistrationpage.h"
#include "ui_owncloudproviderregistrationpage.h"
#include "wizard/owncloudwizardcommon.h"
#include "wizard/owncloudregistrationfinishedpage.h"
#include <QDebug>

namespace OCC
{

OwncloudProviderRegistrationPage::OwncloudProviderRegistrationPage(QWidget *parent) :
    QWizardPage(parent),
    ui(new Ui::OwncloudProviderRegistrationPage)
{
    ui->setupUi(this);
    setTitle(WizardCommon::titleTemplate().arg(tr("Hosting providers")));
    setSubTitle(WizardCommon::subTitleTemplate().arg(tr("find a provider to create a new account")));
    setCommitPage(true);
    ui->labelHint->setText("");
    connect(ui->fieldUsername, SIGNAL(editingFinished()), this, SLOT(verifyData()));
    connect(ui->fieldDisplayname, SIGNAL(editingFinished()), this, SLOT(verifyData()));
    connect(ui->fieldEmail, SIGNAL(editingFinished()), this, SLOT(verifyData()));
    connect(ui->fieldPassword, SIGNAL(editingFinished()), this, SLOT(verifyData()));
}

OwncloudProviderRegistrationPage::~OwncloudProviderRegistrationPage()
{
    delete ui;
}

void OwncloudProviderRegistrationPage::setProvider(OwncloudProviderModel* model)
{
    this->model = model;
    if(model->providerLogoImage() != nullptr) {
        ui->providerLogo->setPixmap(*(model->providerLogoImage()));
        ui->providerLogo->setScaledContents(true);
    }
    ui->providerName->setText(model->providerName());
    ui->providerDescription->setText(model->providerDescription());
    wizard()->setButtonText(QWizard::CommitButton, tr("Create a new account"));
}

bool OwncloudProviderRegistrationPage::hasProvider()
{
    if(model != nullptr)
        return true;
    return false;
}

void OwncloudProviderRegistrationPage::verifyData()
{
    QString username = ui->fieldUsername->text();
    QString displayname = ui->fieldDisplayname->text();
    QString email = ui->fieldEmail->text();

    if (username.isEmpty() || displayname.isEmpty() || email.isEmpty())
        return;

    OcsRegistrationJob *job = new OcsRegistrationJob(model->registrationUrl());
    connect(job, SIGNAL(shareJobFinished(QJsonDocument, QVariant)), SLOT(updateVerifiedData(QJsonDocument,QVariant)));
    connect(job, SIGNAL(ocsError(int, QString)), SLOT(slotOcsError(int, QString)));
    job->verify(username, displayname, email);
    emit completeChanged();
}
void OwncloudProviderRegistrationPage::updateVerifiedData(const QJsonDocument &jsonDocument, const QVariant &value)
{
    _isValid = true;
    ui->labelHint->setText("");
    emit completeChanged();
}

void OwncloudProviderRegistrationPage::finishedRegistration(const QJsonDocument &jsonDocument, const QVariant &value)
{
    _registrationFinished = true;
    ui->labelHint->setText("");
    OwncloudRegistrationFinishedPage *finishedPage;
    finishedPage = qobject_cast<OwncloudRegistrationFinishedPage *>(wizard()->page(WizardCommon::Page_ProviderRegistrationFinished));
    finishedPage->setResult(jsonDocument);
    wizard()->next();
}

void OwncloudProviderRegistrationPage::slotOcsError(int statusCode, const QString &message)
{
    ui->labelHint->setText(message);
    _isValid = false;
    emit completeChanged();
}

bool OwncloudProviderRegistrationPage::isComplete() const
{
    return _isValid && !ui->fieldPassword->text().isEmpty();
}

int OwncloudProviderRegistrationPage::nextId() const
{
    return WizardCommon::Page_ProviderRegistrationFinished;
}

bool OwncloudProviderRegistrationPage::validatePage()
{
    if (_registrationFinished)
        return true;
    _isValid = false;
    QString username = ui->fieldUsername->text();
    QString displayname = ui->fieldDisplayname->text();
    QString email = ui->fieldEmail->text();
    QString password = ui->fieldPassword->text();

    OcsRegistrationJob *job = new OcsRegistrationJob(model->registrationUrl());
    connect(job, SIGNAL(shareJobFinished(QJsonDocument, QVariant)), SLOT(finishedRegistration(QJsonDocument,QVariant)));
    connect(job, SIGNAL(ocsError(int, QString)), SLOT(slotOcsError(int, QString)));
    job->createRegistration(username, displayname, email, password);
    return false;
}


}
