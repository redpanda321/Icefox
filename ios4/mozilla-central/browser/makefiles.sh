# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is the Mozilla build system.
#
# The Initial Developer of the Original Code is
# the Mozilla Foundation <http://www.mozilla.org/>.
# Portions created by the Initial Developer are Copyright (C) 2007
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Henrik Skupin <hskupin@gmail.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

add_makefiles "
browser/Makefile
browser/app/Makefile
browser/app/profile/extensions/Makefile
browser/base/Makefile
browser/components/Makefile
browser/components/build/Makefile
browser/components/certerror/Makefile
browser/components/dirprovider/Makefile
browser/components/feeds/Makefile
browser/components/feeds/public/Makefile
browser/components/feeds/src/Makefile
browser/components/migration/Makefile
browser/components/migration/public/Makefile
browser/components/migration/src/Makefile
browser/components/places/Makefile
browser/components/places/src/Makefile
browser/components/preferences/Makefile
browser/components/privatebrowsing/Makefile
browser/components/privatebrowsing/src/Makefile
browser/components/safebrowsing/Makefile
browser/components/safebrowsing/src/Makefile
browser/components/search/Makefile
browser/components/sessionstore/Makefile
browser/components/sessionstore/src/Makefile
browser/components/sidebar/Makefile
browser/components/sidebar/src/Makefile
browser/components/shell/Makefile
browser/components/shell/public/Makefile
browser/components/shell/src/Makefile
browser/components/wintaskbar/Makefile
browser/fuel/Makefile
browser/fuel/public/Makefile
browser/fuel/src/Makefile
browser/installer/Makefile
browser/installer/windows/Makefile
browser/locales/Makefile
browser/themes/Makefile
browser/themes/pinstripe/browser/Makefile
browser/themes/pinstripe/communicator/Makefile
browser/themes/pinstripe/Makefile
browser/themes/winstripe/browser/Makefile
browser/themes/winstripe/communicator/Makefile
browser/themes/winstripe/Makefile
$MOZ_BRANDING_DIRECTORY/Makefile
$MOZ_BRANDING_DIRECTORY/content/Makefile
$MOZ_BRANDING_DIRECTORY/locales/Makefile
"

if [ "$ENABLE_TESTS" ]; then
  add_makefiles "
    browser/base/content/test/Makefile
    browser/components/certerror/test/Makefile
    browser/components/preferences/tests/Makefile
    browser/components/search/test/Makefile
    browser/components/sessionstore/test/Makefile
    browser/components/sessionstore/test/browser/Makefile
    browser/components/shell/test/Makefile
    browser/components/feeds/test/Makefile
    browser/components/feeds/test/chrome/Makefile
    browser/components/places/tests/Makefile
    browser/components/places/tests/chrome/Makefile
    browser/components/places/tests/browser/Makefile
    browser/components/places/tests/perf/Makefile
    browser/components/privatebrowsing/test/Makefile
    browser/components/privatebrowsing/test/browser/Makefile
    browser/components/safebrowsing/content/test/Makefile
    browser/components/wintaskbar/test/Makefile
    browser/fuel/test/Makefile
  "
fi
