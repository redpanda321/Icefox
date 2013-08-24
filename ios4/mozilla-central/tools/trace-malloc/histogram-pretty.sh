#!/bin/sh
#
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
# The Original Code is historgram-diff.sh, released
# Dec 8, 2000.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 2000
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Chris Waterson <waterson@netscape.com>
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

#   histogram-pretty.sh [-c <count>] [-w <width>] <file>
#
# Pretty-print the histogram in file <file>, displaying at most
# <count> rows.

# How many rows are we gonna show?
COUNT=20
WIDTH=22

# Read arguments
while [ $# -gt 0 ]; do
    case "$1" in
    -c) COUNT=$2
        shift 2
        ;;
    -w) WIDTH=$2
        shift 2
        ;;
    *)  break
        ;;
    esac
done

FILE=$1

# The first `awk' script computes a `TOTAL' row. Then, we sort by the
# larges delta in bytes.
awk '{ tobj += $2; tbytes += $3; } END { print "TOTAL", tobj, tbytes; }' ${FILE} > /tmp/$$.sorted

sort -nr +2 ${FILE} >> /tmp/$$.sorted

# Pretty-print, including percentages
cat <<EOF > /tmp/$$.awk
BEGIN {
  printf "%-${WIDTH}s  Count    Bytes %Total   %Cov\n", "Type";
  }
\$1 == "TOTAL" {
  tbytes = \$3;
  }
NR <= $COUNT {
  if (\$1 != "TOTAL") {
    covered += \$3;
  }
  printf "%-${WIDTH}s %6d %8d %6.2lf %6.2lf\n", \$1, \$2, \$3, 100.0 * \$3 / tbytes, 100.0 * covered / tbytes;
  }
NR > $COUNT {
  oobjs += \$2;  obytes += \$3; covered += \$3;
  }
END {
  printf "%-${WIDTH}s %6d %8d %6.2lf %6.2lf\n", "OTHER", oobjs, obytes, obytes * 100.0 / tbytes, covered * 100.0 / tbytes;
  }
EOF

# Now pretty print the file, and spit it out on stdout.
awk -f /tmp/$$.awk /tmp/$$.sorted

rm -f /tmp/$$.awk /tmp/$$.sorted
