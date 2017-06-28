#ifndef OCSREGISTRATIONJOB_H
#define OCSREGISTRATIONJOB_H

#include "ocsjob.h"
#include "sharemanager.h"
#include <QVector>
#include <QList>
#include <QPair>

namespace OCC {

/**
 * @brief The OcsRegistrationJob class
 * @ingroup gui
 *
 * Handle talking to the OCS Registration API.
 */
class OcsRegistrationJob : public OcsJob
{
    Q_OBJECT
public:
    OcsRegistrationJob(const QString &registrationUrl);
    void verify(const QString &username, const QString &displayname, const QString &email);
    void createRegistration(const QString &username, const QString &displayname, const QString &email, const QString &password);
    void status(const QString &clientSecret);

signals:
    /**
     * Result of the OCS request
     * The value parameter is only set if this was a put request.
     * e.g. if we set the password to 'foo' the QVariant will hold a QString with 'foo'.
     * This is needed so we can update the share objects properly
     *
     * @param reply The reply
     * @param value To what did we set a variable (if we set any).
     */
    void shareJobFinished(QJsonDocument reply, QVariant value);

private slots:
    void jobDone(QJsonDocument reply);

private:
    QVariant _value;
};

}
#endif // OCSREGISTRATIONJOB_H
