# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Usage: ./update.sh <libspeex_src_directory>
#
# Copies the needed files from a directory containing the original
# libspeex sources that we need for HTML5 media playback rate change.
cp $1/libspeex/resample.c src
cp $1/libspeex/arch.h src
cp $1/libspeex/stack_alloc.h src
cp $1/libspeex/fixed_generic.h src
cp $1/include/speex/speex_resampler.h src
cp $1/AUTHORS .
cp $1/COPYING .
