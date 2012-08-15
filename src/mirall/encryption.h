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
#include <QMap>
#include <QList>

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
    void getUserKeys();
    void generateUserKeys();
    void setExpectedReturnValues(QList<QString>);

private:
    QString _baseurl;
    QString _username;
    QString _password;
    QNetworkAccessManager *_nam;
    QList<QString> _returnValues;

    QMap<QString, QString> parseXML(QString xml, QList<QString> tags);

signals:
    void ocsResults(QMap<QString, QString>);

protected slots:
    void slotHttpRequestResults(QNetworkReply*);
    void slotHttpAuth(QNetworkReply*,QAuthenticator*);
    
};

#endif // ENCRYPTION_H
