#include "owncloudregistrationfinishedpage.h"
#include "wizard/owncloudwizardcommon.h"
#include "ui_owncloudregistrationfinishedpage.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
namespace OCC {

OwncloudRegistrationFinishedPage::OwncloudRegistrationFinishedPage(QWidget *parent) :
    QWizardPage(parent),
    ui(new Ui_OwncloudRegistrationFinishedPage)
{
    ui->setupUi(this);
    setTitle(WizardCommon::titleTemplate().arg(tr("Registration")));
    setSubTitle(WizardCommon::subTitleTemplate().arg(tr("You are almost finished.")));
    setFinalPage(true);
}

OwncloudRegistrationFinishedPage::~OwncloudRegistrationFinishedPage()
{
    delete ui;
}

int OwncloudRegistrationFinishedPage::nextId() const
{
    return -1;
}

void OwncloudRegistrationFinishedPage::setResult(const QJsonDocument &jsonDocument)
{
    qDebug() << jsonDocument;
    QString message = jsonDocument.object()["ocs"].toObject()["data"].toObject()["message"].toString();
    ui->label->setText(message);
}

}
