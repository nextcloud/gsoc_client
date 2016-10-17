/*
 * Copyright (C) by Krzesimir Nowak <krzesimir@endocode.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#pragma once

#include <QList>
#include <QMap>
#include <QNetworkCookie>
#include <QUrl>
#include <QPointer>

#include "wizard/abstractcredswizardpage.h"
#include "accountfwd.h"

namespace OCC {

class ShibbolethWebView;

/**
 * @brief The OwncloudBrowserCredsPage class
 * @ingroup gui
 */
class OwncloudBrowserCredsPage : public AbstractCredentialsWizardPage
{
  Q_OBJECT
public:
  OwncloudBrowserCredsPage();

  AbstractCredentials* getCredentials() const Q_DECL_OVERRIDE;

  void initializePage() Q_DECL_OVERRIDE;
  int nextId() const Q_DECL_OVERRIDE;
  void setConnected();

public Q_SLOTS:
  void setVisible(bool visible) Q_DECL_OVERRIDE;
  void slotBrowserRejected();

private:
  bool _afterInitialSetup;
public:
  QString _token;
};

} // namespace OCC
