/*
 * Copyright (C) by Roeland Jago Douma <roeland@famdouma.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "ocsregistrationjob.h"
#include "networkjobs.h"
#include "account.h"
#include "creds/credentialsfactory.h"

#include <QBuffer>
#include <QJsonDocument>

namespace OCC {

OcsRegistrationJob::OcsRegistrationJob(const QString &registrationUrl) : OcsJob(Account::create())
{
    _account->setUrl(registrationUrl);
    _account->setCredentials(CredentialsFactory::create("dummy"));
    connect(this, SIGNAL(jobFinished(QJsonDocument)), SLOT(jobDone(QJsonDocument)));
    addPassStatusCode(200);
}

void OcsRegistrationJob::verify(const QString &username, const QString &displayname, const QString &email)
{
    setVerb("POST");
    setPath("/validate");

    addParam(QString::fromLatin1("username"), username);
    addParam(QString::fromLatin1("displayname"), displayname);
    addParam(QString::fromLatin1("email"), email);

    start();
}

void OcsRegistrationJob::createRegistration(const QString &username, const QString &displayname, const QString &email, const QString &password)
{
    setVerb("POST");

    // FIXME: use proper status code for pending
    addPassStatusCode(404);

    addParam(QString::fromLatin1("username"), username);
    addParam(QString::fromLatin1("displayname"), displayname);
    addParam(QString::fromLatin1("email"), email);
    addParam(QString::fromLatin1("password"), password);

    start();
}

void OcsRegistrationJob::status(const QString &clientSecret)
{
    setVerb("POST");

    // FIXME: use proper status code for pending
    addPassStatusCode(404);

    addParam(QString::fromLatin1("clientSecret"), clientSecret);

    start();
}
void OcsRegistrationJob::jobDone(QJsonDocument reply)
{
    emit shareJobFinished(reply, _value);
}

}
