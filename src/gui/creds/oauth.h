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

#pragma once
#include <QPointer>
#include <QTcpServer>

namespace OCC
{

/**
 * Job that do the authorization grant and fetch the access token
 */
class OAuth : public QObject
{
    Q_OBJECT
public:
    OAuth(Account *account, QObject *parent)
        : QObject(parent), _account(account)
    { }
    ~OAuth();

    enum Result { Waiting, NotSupported, LoggedIn, Error };
    void start();
    bool openBrowser();

signals:
    /**
     * The state has changed.
     * when logged in, token has the value of the token.
     */
    void result(OAuth::Result result, const QString &user = QString(), const QString &token = QString(), const QString &refreshToken = QString());
private:

    Account *_account;
    QTcpServer _server;

    QUrl _pollUrl;
};


} // namespace OCC

