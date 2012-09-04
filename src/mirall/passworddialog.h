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

#ifndef ENCRYPTIONPASSWDDIALOG_H
#define ENCRYPTIONPASSWDDIALOG_H

#include <QDialog>

class Encryption;

namespace Ui {
    class PasswordDialogUi;
}

class PasswordDialog : public QDialog
{
    Q_OBJECT
    
public:
    enum Operations { GenRSAKey,
                      ChangeKeyPasswd,
                      GetKeyPasswd};

    PasswordDialog(QWidget *parent = 0);
    void setOperation(Operations op);
    ~PasswordDialog();

private:
    Operations _operation;
    void updateDialog();

private slots:
    void slotAccept();
    void slotReject();

signals:
    void privateKeyPassword(QString);
    void changePrivateKeyPassword(QString oldpasswd, QString newpasswd);
    
private:
    Ui::PasswordDialogUi *_ui;
};

#endif // ENCRYPTIONPASSWDDIALOG_H
