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
    void setBaseUrl(QString baseurl);
    void setAuthCredentials(QString username, QString password);
    void getUserKeys(QString password);
    void generateUserKeys(QString password);
    void changeUserKeyPassword(QString oldpasswd, QString newpasswd);

private:    
    enum OCSCalls { SetUserKeys,
                    GetUserKeys,
                    ChangeKeyPassword };
    QString _baseurl;
    QString _username;
    QString _password;
    QNetworkAccessManager *_nam;
    QHash<QNetworkReply*, OCSCalls> _directories;
    QHash<QNetworkReply*, QHash<QString, QString> > _data;

    QMap<QString, QString> parseXML(QString xml, QList<QString> tags);
    QMap<QString, QString> key2pem(QString password);
    bool pem2key(QString privatekey, QString password);
    void sendUserKeys(QMap<QString, QString> keypair, OCSCalls operation);
    void onError(QNetworkReply *reply);

signals:
    void ocsGetUserKeysResults(QMap<QString, QString>);
    void ocsSetUserKeysResults(QMap<QString, QString>);
    void ocsChangePasswordResult(QMap<QString, QString>);

protected slots:
    void slotHttpRequestResults(QNetworkReply*);
    void slotHttpAuth(QNetworkReply*,QAuthenticator*);
    
};

#endif // ENCRYPTION_H
