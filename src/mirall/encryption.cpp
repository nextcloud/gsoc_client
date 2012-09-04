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
#include "keymanager.h"
#include <QMap>
#include <iostream>
#include <QtNetwork>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QDomDocument>
#include <QFile>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
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

void Encryption::getUserKeys(QString password)
{
    QNetworkReply *reply = _nam->get(QNetworkRequest(QUrl(_baseurl + "/ocs/v1.php/cloud/userkeys")));
    _directories[reply] = Encryption::GetUserKeys;
    _data[reply].insert("password", password);
}

void Encryption::generateUserKeys(QString password)
{
    RSA *rsa;
    int bits = 1024;	//	512, 1024, 2048, 4096
    unsigned long exp = RSA_F4;	//	RSA_3

    if ( (rsa = RSA_generate_key(bits, exp, NULL, NULL)) == NULL) {
         qDebug() << "Couldn't generate RSA Key";
         return;
    }

    Keymanager::Instance()->setRSAkey(rsa);

    sendUserKeys(key2pem(password), Encryption::SetUserKeys);
}

void Encryption::changeUserKeyPassword(QString oldpasswd, QString newpasswd)
{
    QNetworkReply *reply = _nam->get(QNetworkRequest(QUrl(_baseurl + "/ocs/v1.php/cloud/userkeys")));
    _directories[reply] = Encryption::ChangeKeyPassword;
    _data[reply].insert("oldpassword", oldpasswd);
    _data[reply].insert("newpassword", newpasswd);
    _data[reply].insert("step", "import");
}

void Encryption::sendUserKeys(QMap<QString, QString> keypair, OCSCalls operation)
{
    QUrl postData;

    postData.addQueryItem("privatekey", QUrl::toPercentEncoding(keypair["privatekey"]));
    postData.addQueryItem("publickey", QUrl::toPercentEncoding(keypair["publickey"]));

    QNetworkRequest req;
    req.setUrl(QUrl(_baseurl + "/ocs/v1.php/cloud/userkeys"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = _nam->post(req, postData.encodedQuery());
    _directories[reply] = operation;

    QEventLoop eventLoop;
    // stay here until request is finished to avoid losing postData to early
    connect(_nam, SIGNAL(finished()), &eventLoop, SLOT(quit()));
    eventLoop.exec();
}

QMap<QString, QString> Encryption::key2pem(QString password)
{
    QMap<QString, QString> keypair;
    BUF_MEM *bptr;
    BIO *pubBio = BIO_new(BIO_s_mem());
    BIO *privBio = BIO_new(BIO_s_mem());
    EVP_PKEY *pkey;

    pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, Keymanager::Instance()->getRSAkey());

    PEM_write_bio_RSA_PUBKEY(pubBio, Keymanager::Instance()->getRSAkey());
    PEM_write_bio_PKCS8PrivateKey(privBio, pkey, EVP_aes_128_cfb(), NULL, 0, 0,  password.toLocal8Bit().data());

    BIO_get_mem_ptr(pubBio, &bptr);
    keypair["publickey"] = QString::fromAscii(bptr->data, bptr->length);

    BIO_get_mem_ptr(privBio, &bptr);
    keypair["privatekey"] = QString::fromAscii(bptr->data, bptr->length);

    BIO_free_all(pubBio);
    BIO_free_all(privBio);
    EVP_PKEY_free(pkey);

    return keypair;
}

bool Encryption::pem2key(QString privatekey, QString password)
{
    QByteArray ba = privatekey.toLocal8Bit();
    char *c_privatekey = ba.data();

    BIO *privBio =  BIO_new_mem_buf(c_privatekey, -1);
    RSA *rsa = RSA_new();

    if (PEM_read_bio_RSAPrivateKey(privBio, &rsa, 0, password.toLocal8Bit().data()) == NULL) {
        return false;
    }

    Keymanager::Instance()->setRSAkey(rsa);

    BIO_free_all(privBio);

    return true;
}


/**
 * Qt slots
 */

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
            if (result["statuscode"] == "100") {
                if (!pem2key( result["privatekey"], _data.value(reply).value("password"))) {
                    result["statuscode"] = "-1";
                }
            }
            emit ocsGetUserKeysResults(result);
            break;
        case Encryption::SetUserKeys:
            returnValues << "statuscode";
            result = parseXML(xml, returnValues);
            emit ocsSetUserKeysResults(result);
            break;
        case Encryption::ChangeKeyPassword:
            if (_data.value(reply).constFind("step") !=  _data.value(reply).constEnd()) { // get key and check old password before setting the new one
                returnValues << "statuscode" << "privatekey";
                result = parseXML(xml, returnValues);
                if (result["statuscode"] == "100") {
                    if (!pem2key(result["privatekey"], _data.value(reply).value("oldpassword"))) {
                        result["statuscode"] = "-1";
                        emit ocsChangePasswordResult(result);
                        break;
                    }
                    sendUserKeys(key2pem(_data.value(reply).value("newpassword")), Encryption::ChangeKeyPassword);
                }
            } else { //check if key was uploaded successfully
                returnValues << "statuscode";
                result = parseXML(xml, returnValues);
                emit ocsChangePasswordResult(result);
            }
            break;
        default :
            qDebug() << "Something went wrong!";
        }

    } else {
        qDebug() << reply->errorString();
    }
    _directories.remove(reply);
    _data.remove(reply);
}

void Encryption::slotHttpAuth(QNetworkReply *reply, QAuthenticator *auth)
{
    auth->setUser(_username);
    auth->setPassword(_password);
}

/**
 * some helper functions
 */

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

