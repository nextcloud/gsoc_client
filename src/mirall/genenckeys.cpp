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

#include "genenckeys.h"
#include "ui_genenckeys.h"
#include "mirall/encryption.h"

#include <iostream>

GenEncKeys::GenEncKeys(QWidget *parent) :
    QDialog(parent),
    _ui(new Ui::GenEncKeys)
{
    _ui->setupUi(this);

    connect( _ui->bbFinishSetup, SIGNAL(accepted()), this, SLOT(slotAccept()));
    connect( _ui->bbFinishSetup, SIGNAL(rejected()), this, SLOT(slotReject()));
}

GenEncKeys::~GenEncKeys()
{
    delete _ui;
}

void GenEncKeys::slotAccept()
{
    QString password = _ui->lePassword->text();
    if (  QString::compare(password, _ui->lePasswordRepeated->text(), Qt::CaseSensitive) == 0 ) {
        emit privateKeyPassword(password);
    }
}

void GenEncKeys::slotReject()
{
     std::cout << "rejected" << std::endl << std::flush;
}
