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

#ifndef GENENCKEYS_H
#define GENENCKEYS_H

#include <QDialog>

class Encryption;

namespace Ui {
    class GenEncKeys;
}

class GenEncKeys : public QDialog
{
    Q_OBJECT
    
public:
    explicit GenEncKeys(QString url, QString username, QString password, QWidget *parent = 0);
    ~GenEncKeys();

private slots:
    void slotAccept();
    void slotReject();
    
private:
    Ui::GenEncKeys *_ui;
    Encryption *_enc;
};

#endif // GENENCKEYS_H
