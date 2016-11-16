/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
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

#include <QInputDialog>
#include <QLabel>
#include <QDesktopServices>
#include <QNetworkReply>
#include <QTimer>
#include <QBuffer>
#include "creds/httpcredentialsgui.h"
#include "theme.h"
#include "account.h"
#include <json.h>

using namespace QKeychain;

namespace OCC
{

void HttpCredentialsGui::askFromUser()
{
    _asyncAuth.reset(new AsyncAuth(_account, this));
    connect(_asyncAuth.data(), SIGNAL(result(AsyncAuth::Result,QString)),
            this, SLOT(asyncAuthResult(AsyncAuth::Result,QString)));
    _asyncAuth->start();
}

void HttpCredentialsGui::asyncAuthResult(AsyncAuth::Result r, const QString& token)
{
    if (r == AsyncAuth::Waiting) {
        return;
    }
    if (r == AsyncAuth::NotSupported) {
        // We will re-enter the event loop, so better wait the next iteration
        QMetaObject::invokeMethod(this, "showDialog", Qt::QueuedConnection);
        return;
    }
    if (r == AsyncAuth::LoggedIn) {
        _password = token;
        _ready = true;
        persist();
    }
    emit asked();
}

void HttpCredentialsGui::showDialog()
{
    QString msg = tr("Please enter %1 password:<br>"
                     "<br>"
                     "User: %2<br>"
                     "Account: %3<br>")
                  .arg(Utility::escape(Theme::instance()->appNameGUI()),
                       Utility::escape(_user),
                       Utility::escape(_account->displayName()));

    QString reqTxt = requestAppPasswordText(_account);
    if (!reqTxt.isEmpty()) {
        msg += QLatin1String("<br>") + reqTxt + QLatin1String("<br>");
    }
    if (!_fetchErrorString.isEmpty()) {
        msg += QLatin1String("<br>") + tr("Reading from keychain failed with error: '%1'").arg(
                    Utility::escape(_fetchErrorString)) + QLatin1String("<br>");
    }

    QInputDialog dialog;
    dialog.setWindowTitle(tr("Enter Password"));
    dialog.setLabelText(msg);
    dialog.setTextValue(_previousPassword);
    dialog.setTextEchoMode(QLineEdit::Password);
    if (QLabel *dialogLabel = dialog.findChild<QLabel *>()) {
        dialogLabel->setOpenExternalLinks(true);
        dialogLabel->setTextFormat(Qt::RichText);
    }

    bool ok = dialog.exec();
    if (ok) {
        _password = dialog.textValue();
        _ready = true;
        persist();
    }
    emit asked();
}

QString HttpCredentialsGui::requestAppPasswordText(const Account* account)
{
    if (account->serverVersionInt() < 0x090100) {
        // Older server than 9.1 does not have trhe feature to request App Password
        return QString();
    }

    return tr("<a href=\"%1\">Click here</a> to request an app password from the web interface.")
        .arg(account->url().toString() + QLatin1String("/index.php/settings/personal?section=apppasswords"));
}

AsyncAuth::~AsyncAuth()
{
    delete _reply;
}

void AsyncAuth::start()
{
    QNetworkRequest req;
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    auto buffer = new QBuffer();
    buffer->setData("name=" + Utility::userAgentString().toPercentEncoding());
    QNetworkReply *reply = _account->davRequest("POST",
            Utility::concatUrlPath(_account->url(), QLatin1String("/index.php/auth/start")),
            req, buffer);
    QObject::connect(reply, SIGNAL(finished()), this, SLOT(startFinished()));
    buffer->setParent(reply);
    QTimer::singleShot(30*1000, reply, SLOT(abort()));
    _reply = reply;

    // FIXME! timeout

//     _state = AsyncAuth::QueryUrlState;
}

// We get a reply from /index.php/auth/start
void AsyncAuth::startFinished()
{
    auto reply = qobject_cast<QNetworkReply *>(sender());
    Q_ASSERT(reply);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "auth/start returned error" << reply->errorString();

        // FIXME! we should make a difference between network errors and file not found error.
        emit result(NotSupported, QString());
        return;
    }


    const QString replyData = reply->readAll();

    bool success;
    QVariantMap json = QtJson::parse(replyData, success).toMap();
    if (!success) {
        qDebug() << "could not parse json" << replyData;
        emit result(NotSupported, QString());
        return;
    }
    auto clientUrl = QUrl::fromEncoded(json[QLatin1String("clientUrl")].toByteArray());
    _pollUrl = QUrl::fromEncoded(json[QLatin1String("pollUrl")].toByteArray());
    if (!_pollUrl.isValid() || !clientUrl.isValid()) {
        qWarning() << "invalid urls from server" << json;
        emit result(NotSupported, QString());
        return;
    }
    if (!QDesktopServices::openUrl(clientUrl)) {
        // We cannot open the browser, then we claim we don't support it.
        emit result(NotSupported, QString());
        return;
    }
    emit result(Waiting, QString());
    QTimer::singleShot(1000, this, SLOT(poll()));
}

void AsyncAuth::poll()
{
    QNetworkReply *reply = _account->davRequest("POST", _pollUrl, QNetworkRequest());
    QObject::connect(reply, SIGNAL(finished()), this, SLOT(pollFinished()));
    QTimer::singleShot(5*60*1000, reply, SLOT(abort()));
    _reply = reply;
}

void AsyncAuth::pollFinished()
{
    auto reply = qobject_cast<QNetworkReply *>(sender());
    Q_ASSERT(reply);

    const QString replyData = reply->readAll();
    reply->deleteLater();

    bool success;
    QVariantMap json = QtJson::parse(replyData, success).toMap();
    if (!success || json["status"].toUInt() != 1) {
        QTimer::singleShot(4*1000, this, SLOT(poll()));
        return;
    }

    auto token = json["token"].toString();

    emit result(LoggedIn, token);
}


} // namespace OCC
