#ifndef ENCRYPTION_H
#define ENCRYPTION_H

/*
 * Copyright (C) 2012 Bjoern Schiessle <schiessle@owncloud.com>
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

#include <QObject>
#include <QList>
#include <QMap>
#include <QHash>
#include <openssl/rsa.h>

class QNetworkAccessManager;
class QNetworkReply;
class QAuthenticator;

class Encryption : public QObject
{
    Q_OBJECT
public:
    explicit Encryption(QString baseurl = "", QString username = "", QString password = "", QObject *parent = 0);
    ~Encryption();
    void setBaseUrl(QString baseurl);
    void setAuthCredentials(QString username, QString password);
    void getUserKeys();
    void generateUserKeys(QString password);

private:    
    enum OCSCalls { SetUserKeys,
                    GetUserKeys };
    RSA *rsa;
    QString _baseurl;
    QString _username;
    QString _password;
    QNetworkAccessManager *_nam;
    QHash<QNetworkReply*, OCSCalls> _directories;

    QMap<QString, QString> parseXML(QString xml, QList<QString> tags);
    QMap<QString, QString> key2pem(QString password);
    void sendUserKeys(QMap<QString, QString> keypair);

signals:
    void ocsGetUserKeysResults(QMap<QString, QString>);
    void ocsSetUserKeysResults(QMap<QString, QString>);

protected slots:
    void slotHttpRequestResults(QNetworkReply*);
    void slotHttpAuth(QNetworkReply*,QAuthenticator*);
    
};

#endif // ENCRYPTION_H
