# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Usage: update-breakpad.sh <path to breakpad SVN>

if [ $# -lt 1 ]; then
  echo "Usage: update-breakpad.sh /path/to/google-breakpad/"
  exit 1
fi

crashreporter_dir=`dirname $0`
repo=${crashreporter_dir}/../..
rm -rf ${crashreporter_dir}/google-breakpad
svn export $1 ${crashreporter_dir}/google-breakpad

# remove some extraneous bits
rm -rf ${crashreporter_dir}/google-breakpad/src/third_party/protobuf ${crashreporter_dir}/google-breakpad/src/testing/ ${crashreporter_dir}/google-breakpad/src/tools/gyp/
# restore our Makefile.ins
hg -R ${repo} st -n | grep "Makefile\.in$" | xargs hg revert --no-backup
# and some other makefiles
hg -R ${repo} st -n | grep "objs\.mk$" | xargs hg revert --no-backup

# Record `svn info`
svn info $1 > ${crashreporter_dir}/google-breakpad/SVN-INFO

hg addremove ${crashreporter_dir}/google-breakpad/
