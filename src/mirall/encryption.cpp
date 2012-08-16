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

#include "encryption.h"
#include <QMap>
#include <iostream>
#include <QtNetwork>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QDomDocument>
#include <QFile>
#include <openssl/rsa.h>
#include <openssl/aes.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <stdio.h>

Encryption::Encryption(QString baseurl, QString username, QString password, QObject *parent) :
    QObject(parent)
{
    setBaseUrl(baseurl);
    setAuthCredentials(username, password);
    _nam = new QNetworkAccessManager(this);

    connect( _nam, SIGNAL(finished(QNetworkReply*)),this,SLOT(slotHttpRequestResults(QNetworkReply*)));
    connect( _nam, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)), this, SLOT(slotHttpAuth(QNetworkReply*,QAuthenticator*)));
}

void Encryption::setBaseUrl(QString baseurl)
{
    _baseurl = baseurl;
}

void Encryption::setAuthCredentials(QString username, QString password) {
    _username = username;
    _password = password;
}

QMap<QString, QString> Encryption::parseXML(QString xml, QList<QString> tags)
{
    QMap<QString, QString> result;

    QDomDocument doc("results");
    doc.setContent(xml);

    for (int i = 0; i < tags.size(); ++i) {
        QString tag = tags.at(i);
        QDomNodeList elements = doc.elementsByTagName(tag);
        if (elements.size() ==  1 ) {
            result[tag] = elements.at(0).toElement().text();
        }
    }
    return result;
}

void Encryption::getUserKeys()
{
    QNetworkReply *reply = _nam->get(QNetworkRequest(QUrl(_baseurl + "/ocs/v1.php/cloud/userkeys")));
    _directories[reply] = Encryption::GetUserKeys;
}

void Encryption::generateUserKeys(QString password)
{
    QMap<QString, QString> keys = generateKeys(password);

    QUrl postData;
    postData.addQueryItem("privatekey", keys["privatekey"]);
    postData.addQueryItem("publickey", keys["publickey"]);

    QNetworkRequest req;
    req.setUrl(QUrl(_baseurl + "/ocs/v1.php/cloud/userkeys"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = _nam->post(req, postData.encodedQuery());
    _directories[reply] = Encryption::SetUserKeys;

    QEventLoop eventLoop;
    // stay here until request is finished to avoid losing postData to early
    connect(_nam, SIGNAL(finished()), &eventLoop, SLOT(quit()));
    eventLoop.exec();
}

QMap<QString, QString> Encryption::generateKeys(QString password)
{
    FILE *fp;

    RSA *rsa;
    EVP_PKEY *pkey;

    int bits = 1024;	//	512, 1024, 2048, 4096
    unsigned long exp = RSA_F4;	//	RSA_3
    QMap<QString, QString> result;

    rsa = RSA_generate_key(bits, exp, NULL, NULL);

    pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, rsa);

    //TODO really ugly work arround (first write key to file to disk and than read it again), please fix me!!!

    //	write private key to text file
    fp = fopen("tmp_priv.pem", "w");
    PEM_write_PrivateKey(fp, pkey, NULL,NULL,0,NULL,NULL);
    fclose(fp);

    // write public key to text file
    fp = fopen("tmp_pub.pem", "w");
    PEM_write_PUBKEY(fp, pkey);
    fclose(fp);

    QString pubkey;
    QString privkey;
    QFile file("tmp_pub.pem");
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream in(&file);
    while (!in.atEnd()) {
        pubkey += in.readLine() + '\n';
    }
    file.close();
    file.remove();

    file.setFileName("tmp_priv.pem");
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    in.setDevice(&file);
    while (!in.atEnd()) {
        privkey += in.readLine() + '\n';
    }
    file.close();
    file.remove();

    result["privatekey"] = encryptPrivateKey(privkey, password);
    result["publickey"] = pubkey;

    RSA_free(rsa);

    return result;
}

QString Encryption::encryptPrivateKey(QString privkey, QString password)
{
    AES_KEY aesKey;
    int usedBytes;
    unsigned char *IV = NULL;
    IV = (unsigned char*)qstrdup(privkey.toAscii().constData()); //dummy
    int dataLength = 1024; //dummy

    AES_set_encrypt_key((unsigned char*)qstrdup(password.toAscii().constData()), 128, &aesKey);

    unsigned char *privateKey = NULL;
    privateKey = (unsigned char*)qstrdup(privkey.toAscii().constData());

    // TODO IV, dataLength has to be defined
    AES_cfb128_encrypt(privateKey, privateKey, dataLength, &aesKey, IV, &usedBytes, AES_ENCRYPT);

    return QString(privateKey);
}

void Encryption::slotHttpRequestResults(QNetworkReply *reply)
{
    OCSCalls call = _directories.value(reply);
    QMap<QString, QString> result;
    QList<QString> returnValues;

    if(reply->error() == QNetworkReply::NoError) {

        QString xml =  reply->readAll();

        switch (call)
        {
        case Encryption::GetUserKeys:
            returnValues << "statuscode" << "publickey" << "privatekey";
            result = parseXML(xml, returnValues);
            emit ocsGetUserKeysResults(result);
            break;
        case Encryption::SetUserKeys:
            returnValues << "statuscode";
            result = parseXML(xml, returnValues);
            emit ocsSetUserKeysResults(result);
            break;
        default :
            qDebug() << "Something went wrong!";
        }

    } else {
        qDebug() << reply->errorString();
    }
    _directories.remove(reply);
}

void Encryption::slotHttpAuth(QNetworkReply *reply, QAuthenticator *auth)
{
    auth->setUser(_username);
    auth->setPassword(_password);
}
