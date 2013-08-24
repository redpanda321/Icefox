#! /bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

MOZ_APP_BASENAME=Firefox
MOZ_APP_VENDOR=Mozilla
MOZ_UPDATER=1
MOZ_PHOENIX=1

if test "$OS_ARCH" = "WINNT"; then
  if ! test "$HAVE_64BIT_OS"; then
    MOZ_VERIFY_MAR_SIGNATURE=1
    MOZ_MAINTENANCE_SERVICE=1
  fi
fi

MOZ_CHROME_FILE_FORMAT=omni
MOZ_SAFE_BROWSING=1
MOZ_SERVICES_AITC=1
MOZ_SERVICES_NOTIFICATIONS=1
MOZ_SERVICES_SYNC=1
MOZ_APP_VERSION=$FIREFOX_VERSION
MOZ_EXTENSIONS_DEFAULT=" gnomevfs"
# MOZ_APP_DISPLAYNAME will be set by branding/configure.sh
# Changing MOZ_*BRANDING_DIRECTORY requires a clobber to ensure correct results,
# because branding dependencies are broken.
# MOZ_BRANDING_DIRECTORY is the default branding directory used when none is
# specified. It should never point to the "official" branding directory.
# For mozilla-beta, mozilla-release, or mozilla-central repositories, use
# "nightly" branding (until bug 659568 is fixed).
# For the mozilla-aurora repository, use "aurora".
MOZ_BRANDING_DIRECTORY=browser/branding/nightly
MOZ_OFFICIAL_BRANDING_DIRECTORY=browser/branding/official
MOZ_APP_ID={ec8030f7-c20a-464f-9b0e-13a3a9e97384}
# This should usually be the same as the value MAR_CHANNEL_ID.
# If more than one ID is needed, then you should use a comma separated list
# of values.
ACCEPTED_MAR_CHANNEL_IDS=firefox-mozilla-central
# The MAR_CHANNEL_ID must not contain the following 3 characters: ",\t "
MAR_CHANNEL_ID=firefox-mozilla-central
MOZ_PROFILE_MIGRATOR=1
MOZ_EXTENSION_MANAGER=1
MOZ_APP_STATIC_INI=1
MOZ_WEBAPP_RUNTIME=1
MOZ_MEDIA_NAVIGATOR=1
