/*
 * Copyright (C) by Roeland Jago Douma <rullzer@owncloud.com>
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

#ifndef SHAREMANAGER_H
#define SHAREMANAGER_H

#include "accountfwd.h"
#include "sharee.h"
#include "sharepermissions.h"

#include <QObject>
#include <QDate>
#include <QString>
#include <QList>
#include <QSharedPointer>
#include <QUrl>

class QJsonDocument;
class QJsonObject;

namespace OCC {

class Share : public QObject
{
    Q_OBJECT

public:
    /**
     * Possible share types
     * Need to be in sync with Sharee::Type
     */
    enum ShareType {
        TypeUser = Sharee::User,
        TypeGroup = Sharee::Group,
        TypeLink = 3,
        TypeRemote = Sharee::Federated
    };

    typedef SharePermissions Permissions;

    /*
     * Constructor for shares
     */
    explicit Share(AccountPtr account,
        const QString &id,
        const QString &path,
        const ShareType shareType,
        const Permissions permissions = SharePermissionDefault,
        const QSharedPointer<Sharee> shareWith = QSharedPointer<Sharee>(NULL));

    /**
     * The account the share is defined on.
     */
    AccountPtr account() const;

    /*
     * Get the id
     */
    QString getId() const;

    /*
     * Get the shareType
     */
    ShareType getShareType() const;

    /*
     * Get the shareWith
     */
    QSharedPointer<Sharee> getShareWith() const;

    /*
     * Get permissions
     */
    Permissions getPermissions() const;

    /*
     * Set the permissions of a share
     *
     * On success the permissionsSet signal is emitted
     * In case of a server error the serverError signal is emitted.
     */
    void setPermissions(Permissions permissions);

    /**
     * Deletes a share
     *
     * On success the shareDeleted signal is emitted
     * In case of a server error the serverError signal is emitted.
     */
    void deleteShare();

signals:
    void permissionsSet();
    void shareDeleted();
    void serverError(int code, const QString &message);

protected:
    AccountPtr _account;
    QString _id;
    QString _path;
    ShareType _shareType;
    Permissions _permissions;
    QSharedPointer<Sharee> _shareWith;

protected slots:
    void slotOcsError(int statusCode, const QString &message);

private slots:
    void slotDeleted();
    void slotPermissionsSet(const QJsonDocument &, const QVariant &value);
};

/**
 * A Link share is just like a regular share but then slightly different.
 * There are several methods in the API that either work differently for
 * link shares or are only available to link shares.
 */
class LinkShare : public Share
{
    Q_OBJECT
public:
    explicit LinkShare(AccountPtr account,
        const QString &id,
        const QString &path,
        const QString &name,
        const QString &token,
        const Permissions permissions,
        bool passwordSet,
        const QUrl &url,
        const QDate &expireDate);

    /*
     * Get the share link
     */
    QUrl getLink() const;

    /*
     * The share's link for direct downloading.
     */
    QUrl getDirectDownloadLink() const;

    /*
     * Get the publicUpload status of this share
     */
    bool getPublicUpload() const;

    /*
     * Whether directory listings are available (READ permission)
     */
    bool getShowFileListing() const;

    /*
     * Returns the name of the link share. Can be empty.
     */
    QString getName() const;

    /*
     * Set the name of the link share.
     *
     * Emits either nameSet() or serverError().
     */
    void setName(const QString &name);

    /*
     * Returns the token of the link share.
     */
    QString getToken() const;

    /*
     * Set the password
     *
     * On success the passwordSet signal is emitted
     * In case of a server error the serverError signal is emitted.
     */
    void setPassword(const QString &password);

    /*
     * Is the password set?
     */
    bool isPasswordSet() const;

    /*
     * Get the expiration date
     */
    QDate getExpireDate() const;

    /*
     * Set the expiration date
     *
     * On success the expireDateSet signal is emitted
     * In case of a server error the serverError signal is emitted.
     */
    void setExpireDate(const QDate &expireDate);

signals:
    void expireDateSet();
    void passwordSet();
    void passwordSetError(int statusCode, const QString &message);
    void nameSet();

private slots:
    void slotPasswordSet(const QJsonDocument &, const QVariant &value);
    void slotExpireDateSet(const QJsonDocument &reply, const QVariant &value);
    void slotSetPasswordError(int statusCode, const QString &message);
    void slotNameSet(const QJsonDocument &, const QVariant &value);

private:
    QString _name;
    QString _token;
    bool _passwordSet;
    QDate _expireDate;
    QUrl _url;
};

/**
 * The share manager allows for creating, retrieving and deletion
 * of shares. It abstracts away from the OCS Share API, all the usages
 * shares should talk to this manager and not use OCS Share Job directly
 */
class ShareManager : public QObject
{
    Q_OBJECT
public:
    explicit ShareManager(AccountPtr _account, QObject *parent = NULL);

    /**
     * Tell the manager to create a link share
     *
     * @param path The path of the linkshare relative to the user folder on the server
     * @param name The name of the created share, may be empty
     * @param password The password of the share, may be empty
     *
     * On success the signal linkShareCreated is emitted
     * For older server the linkShareRequiresPassword signal is emitted when it seems appropiate
     * In case of a server error the serverError signal is emitted
     */
    void createLinkShare(const QString &path,
        const QString &name,
        const QString &password);

    /**
     * Tell the manager to create a new share
     *
     * @param path The path of the share relative to the user folder on the server
     * @param shareType The type of share (TypeUser, TypeGroup, TypeRemote)
     * @param Permissions The share permissions
     *
     * On success the signal shareCreated is emitted
     * In case of a server error the serverError signal is emitted
     */
    void createShare(const QString &path,
        const Share::ShareType shareType,
        const QString shareWith,
        const Share::Permissions permissions);

    /**
     * Fetch all the shares for path
     *
     * @param path The path to get the shares for relative to the users folder on the server
     *
     * On success the sharesFetched signal is emitted
     * In case of a server error the serverError signal is emitted
     */
    void fetchShares(const QString &path);

signals:
    void shareCreated(const QSharedPointer<Share> &share);
    void linkShareCreated(const QSharedPointer<LinkShare> &share);
    void sharesFetched(const QList<QSharedPointer<Share>> &shares);
    void serverError(int code, const QString &message);

    /** Emitted when creating a link share with password fails.
     *
     * @param message the error message reported by the server
     *
     * See createLinkShare().
     */
    void linkShareRequiresPassword(const QString &message);

private slots:
    void slotSharesFetched(const QJsonDocument &reply);
    void slotLinkShareCreated(const QJsonDocument &reply);
    void slotShareCreated(const QJsonDocument &reply);
    void slotOcsError(int statusCode, const QString &message);
    void slotCreateShare(const QJsonDocument &reply);

private:
    QSharedPointer<LinkShare> parseLinkShare(const QJsonObject &data);
    QSharedPointer<Share> parseShare(const QJsonObject &data);

    QMap<QObject *, QVariant> _jobContinuation;
    AccountPtr _account;
};
}

#endif // SHAREMANAGER_H
