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

#include <QVariant>

#include "wizard/owncloudbrowsercredspage.h"
#include "theme.h"
#include "account.h"
#include "cookiejar.h"
#include "wizard/owncloudwizardcommon.h"
#include "wizard/owncloudwizard.h"
#include "creds/httpcredentialsgui.h"

namespace OCC
{

OwncloudBrowserCredsPage::OwncloudBrowserCredsPage()
    : AbstractCredentialsWizardPage(),
      _afterInitialSetup(false)

{}

void OwncloudBrowserCredsPage::setVisible(bool visible)
{
    if (!_afterInitialSetup) {
        QWizardPage::setVisible(visible);
        return;
    }

    if (isVisible() == visible) {
        return;
    }
    if (visible) {
        wizard()->hide();
    } else {
        wizard()->show();
    }
}

void OwncloudBrowserCredsPage::initializePage()
{
    _afterInitialSetup = true;
}

int OwncloudBrowserCredsPage::nextId() const
{
  return WizardCommon::Page_AdvancedSetup;
}

void OwncloudBrowserCredsPage::setConnected()
{
    wizard()->show();
}

AbstractCredentials* OwncloudBrowserCredsPage::getCredentials() const
{
    OwncloudWizard* ocWizard = qobject_cast< OwncloudWizard* >(wizard());
    Q_ASSERT(ocWizard);

    // FIXME TODO !!!!!!!
    QString user = QLatin1String("owncloud"); // FIXME! username;
    return new HttpCredentialsGui(user, _token, ocWizard->ownCloudCertificatePath, ocWizard->ownCloudCertificatePasswd);
}

void OwncloudBrowserCredsPage::slotBrowserRejected()
{
    wizard()->back();
    wizard()->show();
}

} // namespace OCC
