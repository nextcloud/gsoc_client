#ifndef OWNCLOUDREGISTRATIONFINISHEDPAGE_H
#define OWNCLOUDREGISTRATIONFINISHEDPAGE_H

#include <QWizardPage>
#include "ui_owncloudregistrationfinishedpage.h"
namespace OCC {

class OwncloudRegistrationFinishedPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit OwncloudRegistrationFinishedPage(QWidget *parent = 0);
    ~OwncloudRegistrationFinishedPage();
    int nextId() const;
    void setResult(const QJsonDocument &jsonDocument);

private:
    Ui_OwncloudRegistrationFinishedPage *ui;
};

}
#endif // OWNCLOUDREGISTRATIONFINISHEDPAGE_H
