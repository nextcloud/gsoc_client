set( APPLICATION_NAME       "ownCloud" )
set( APPLICATION_EXECUTABLE "owncloud" )
set( APPLICATION_DOMAIN     "owncloud.com" )
set( APPLICATION_VENDOR     "ownCloud" )
set( APPLICATION_UPDATE_URL "https://updates.owncloud.com/client/" CACHE string "URL for updater" )
set( APPLICATION_ICON_NAME  "owncloud" )

set( THEME_CLASS            "ownCloudTheme" )
set( APPLICATION_REV_DOMAIN "com.owncloud.desktopclient" )
set( WIN_SETUP_BITMAP_PATH  "${CMAKE_SOURCE_DIR}/admin/win/nsi" )

set( MAC_INSTALLER_BACKGROUND_FILE "${CMAKE_SOURCE_DIR}/admin/osx/installer-background.png" CACHE STRING "The MacOSX installer background image")

# set( THEME_INCLUDE          "${OEM_THEME_DIR}/mytheme.h" )
# set( APPLICATION_LICENSE    "${OEM_THEME_DIR}/license.txt )

# set( APPLICATION_PROVIDERS  "https://bitgrid.net/~jus/providers.json.1" )
# set( APPLICATION_SERVERSETUP "https://nextcloud.com/install/#instructions-server" )

option( WITH_CRASHREPORTER "Build crashreporter" OFF )
set( CRASHREPORTER_SUBMIT_URL "https://crash-reports.owncloud.com/submit" CACHE string "URL for crash reporter" )
set( CRASHREPORTER_ICON ":/owncloud-icon.png" )
