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

#include "passworddialog.h"
#include "ui_passworddialog.h"
#include "mirall/encryption.h"

#include <iostream>

PasswordDialog::PasswordDialog(QWidget *parent) :
    QDialog(parent),
    _ui(new Ui::PasswordDialogUi)
{
    _ui->setupUi(this);

    connect( _ui->bbFinishSetup, SIGNAL(accepted()), this, SLOT(slotAccept()));
    connect( _ui->bbFinishSetup, SIGNAL(rejected()), this, SLOT(slotReject()));
}

PasswordDialog::~PasswordDialog()
{
    delete _ui;
}

void PasswordDialog::setOperation(Operations op)
{
    _operation = op;
    updateDialog();
}

void PasswordDialog::updateDialog()
{
    switch (_operation)
    {
    case PasswordDialog::GenRSAKey:
        _ui->gbInstructions->setTitle("Provide Encryption Key Password");
        _ui->instructionsLabel->setText("Please enter the password you want to use to secure your encryption key.\n\n"
                                        "Please choose a strong passwort which is different to your ownCloud password.");
        _ui->lePassword->show();
        _ui->PasswordLabel->show();
        _ui->lePasswordRepeated->show();
        _ui->RepeatPasswordLabel->show();
        _ui->OldPasswordLabel->hide();
        _ui->leOldPassword->hide();
        break;
    case PasswordDialog::GetKeyPasswd:
        _ui->gbInstructions->setTitle("Provide Encryption Key Password");
        _ui->instructionsLabel->setText("Please enter the password for your encryption key.");
        _ui->lePassword->show();
        _ui->PasswordLabel->show();
        _ui->lePasswordRepeated->hide();
        _ui->RepeatPasswordLabel->hide();
        _ui->OldPasswordLabel->hide();
        _ui->leOldPassword->hide();
        break;
    case PasswordDialog::ChangeKeyPasswd:
        _ui->gbInstructions->setTitle("Change Encryption Key Password");
        _ui->instructionsLabel->setText("Please enter the old and the new password for your encryption key.");
        _ui->PasswordLabel->setText("New password");
        _ui->RepeatPasswordLabel->setText("Repeat new password");
        _ui->lePassword->show();
        _ui->PasswordLabel->show();
        _ui->lePasswordRepeated->show();
        _ui->RepeatPasswordLabel->show();
        _ui->OldPasswordLabel->show();
        _ui->leOldPassword->show();
        break;
    }
}

void PasswordDialog::slotAccept()
{
    QString password;

    switch (_operation)
    {
    case PasswordDialog::GenRSAKey:
        if (  QString::compare(_ui->lePassword->text(), _ui->lePasswordRepeated->text(), Qt::CaseSensitive) == 0 ) {
            emit privateKeyPassword(_ui->lePassword->text());
        }
        break;
    case PasswordDialog::GetKeyPasswd:
        emit privateKeyPassword(_ui->lePassword->text());

        break;
    case PasswordDialog::ChangeKeyPasswd:
        if (  QString::compare(_ui->lePassword->text(), _ui->lePasswordRepeated->text(), Qt::CaseSensitive) == 0 ) {
            emit changePrivateKeyPassword(_ui->leOldPassword->text(), _ui->lePassword->text());
        }
        break;
    }

    emit privateKeyPassword(password);
}

void PasswordDialog::slotReject()
{
     std::cout << "rejected" << std::endl << std::flush;
}
