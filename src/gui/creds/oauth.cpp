/*
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

#include <QDesktopServices>
#include <QNetworkReply>
#include <QTimer>
#include "account.h"
#include "creds/oauth.h"
#include <QJsonObject>
#include <QJsonDocument>
#include "theme.h"

namespace OCC {

Q_LOGGING_CATEGORY(lcOauth, "sync.credentials.oauth", QtInfoMsg)

OAuth::~OAuth()
{
}

static void httpReplyAndClose(QTcpSocket *socket, const char *code, const char *html,
    const char *moreHeaders = nullptr)
{
    socket->write("HTTP/1.1 ");
    socket->write(code);
    socket->write("\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: ");
    socket->write(QByteArray::number(qstrlen(html)));
    if (moreHeaders) {
        socket->write("\r\n");
        socket->write(moreHeaders);
    }
    socket->write("\r\n\r\n");
    socket->write(html);
    socket->disconnectFromHost();
    // We don't want that deleting the server too early prevent queued data to be sent on this socket.
    // The socket will be deleted after disconnection because disconnected is connected to deleteLater
    socket->setParent(nullptr);
}

void OAuth::start()
{
    // Listen on the socket to get a port which will be used in the redirect_uri
    if (!_server.listen(QHostAddress::LocalHost)) {
        emit result(NotSupported, QString());
        return;
    }

    if (!openBrowser())
        return;

    QObject::connect(&_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = _server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            QObject::connect(socket, &QIODevice::readyRead, this, [this, socket] {
                QByteArray peek = socket->peek(qMin(socket->bytesAvailable(), 4000LL)); //The code should always be within the first 4K
                if (peek.indexOf('\n') < 0)
                    return; // wait until we find a \n
                QRegExp rx("^GET /\\?code=([a-zA-Z0-9]+)[& ]"); // Match a  /?code=...  URL
                if (rx.indexIn(peek) != 0) {
                    httpReplyAndClose(socket, "404 Not Found", "<html><head><title>404 Not Found</title></head><body><center><h1>404 Not Found</h1></center></body></html>");
                    return;
                }

                QString code = rx.cap(1); // The 'code' is the first capture of the regexp

                QUrl requestToken(_account->url().toString()
                    + QLatin1String("/index.php/apps/oauth2/api/v1/token?grant_type=authorization_code&code=")
                    + code
                    + QLatin1String("&redirect_uri=http://localhost:") + QString::number(_server.serverPort()));
                requestToken.setUserName(Theme::instance()->oauthClientId());
                requestToken.setPassword(Theme::instance()->oauthClientSecret());
                QNetworkRequest req;
                req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
                auto reply = _account->sendRequest("POST", requestToken, req);
                QTimer::singleShot(30 * 1000, reply, &QNetworkReply::abort);
                QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, socket] {
                    auto jsonData = reply->readAll();
                    QJsonParseError jsonParseError;
                    QJsonObject json = QJsonDocument::fromJson(jsonData, &jsonParseError).object();
                    QString accessToken = json["access_token"].toString();
                    QString refreshToken = json["refresh_token"].toString();
                    QString user = json["user_id"].toString();
                    QUrl messageUrl = json["message_url"].toString();

                    if (reply->error() != QNetworkReply::NoError || jsonParseError.error != QJsonParseError::NoError
                        || json.isEmpty() || refreshToken.isEmpty() || accessToken.isEmpty()
                        || json["token_type"].toString() != QLatin1String("Bearer")) {
                        QString errorReason;
                        QString errorFromJson = json["error"].toString();
                        if (!errorFromJson.isEmpty()) {
                            errorReason = tr("Error returned from the server: <em>%1</em>")
                                              .arg(errorFromJson.toHtmlEscaped());
                        } else if (reply->error() != QNetworkReply::NoError) {
                            errorReason = tr("There was an error accessing the 'token' endpoint: <br><em>%1</em>")
                                              .arg(reply->errorString().toHtmlEscaped());
                        } else if (jsonParseError.error != QJsonParseError::NoError) {
                            errorReason = tr("Could not parse the JSON returned from the server: <br><em>%1</em>")
                                              .arg(jsonParseError.errorString());
                        } else {
                            errorReason = tr("The reply from the server did not contain all expected fields");
                        }
                        qCWarning(lcOauth) << "Error when getting the accessToken" << json << errorReason;
                        httpReplyAndClose(socket, "500 Internal Server Error",
                            tr("<h1>Login Error</h1><p>%1</p>").arg(errorReason).toUtf8().constData());
                        emit result(Error);
                        return;
                    }
                    if (!_expectedUser.isNull() && user != _expectedUser) {
                        // Connected with the wrong user
                        QString message = tr("<h1>Wrong user</h1>"
                                             "<p>You logged-in with user <em>%1</em>, but must login with user <em>%2</em>.<br>"
                                             "Please log out of %3 in another tab, then <a href='%4'>click here</a> "
                                             "and log in as user %2</p>")
                                              .arg(user, _expectedUser, Theme::instance()->appNameGUI(),
                                                  authorisationLink().toString(QUrl::FullyEncoded));
                        httpReplyAndClose(socket, "200 OK", message.toUtf8().constData());
                        // We are still listening on the socket so we will get the new connection
                        return;
                    }
                    const char *loginSuccessfullHtml = "<h1>Login Successful</h1><p>You can close this window.</p>";
                    if (messageUrl.isValid()) {
                        httpReplyAndClose(socket, "303 See Other", loginSuccessfullHtml,
                            QByteArray("Location: " + messageUrl.toEncoded()).constData());
                    } else {
                        httpReplyAndClose(socket, "200 OK", loginSuccessfullHtml);
                    }
                    emit result(LoggedIn, user, accessToken, refreshToken);
                });
            });
        }
    });
    QTimer::singleShot(5 * 60 * 1000, this, [this] { result(Error); });
}

QUrl OAuth::authorisationLink() const
{
    Q_ASSERT(_server.isListening());
    QUrl url = QUrl(_account->url().toString()
        + QLatin1String("/index.php/apps/oauth2/authorize?response_type=code&client_id=")
        + Theme::instance()->oauthClientId()
        + QLatin1String("&redirect_uri=http://localhost:") + QString::number(_server.serverPort()));
    if (!_expectedUser.isNull())
        url.addQueryItem("user", _expectedUser);
    return url;
}

bool OAuth::openBrowser()
{
    if (!QDesktopServices::openUrl(authorisationLink())) {
        // We cannot open the browser, then we claim we don't support OAuth.
        emit result(NotSupported, QString());
        return false;
    }
    return true;
}

} // namespace OCC
