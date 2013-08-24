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
# Portions created by the Initial Developer are Copyright (C) 2006
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Benjamin Smedberg <benjamin@smedbergs.us> (Initial Code)
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

ifdef LIBXUL_SDK
$(error toolkit-tiers.mk is not compatible with --enable-libxul-sdk=)
endif

include $(topsrcdir)/config/nspr/build.mk
include $(topsrcdir)/config/js/build.mk

TIERS += platform

ifdef NS_TRACE_MALLOC
tier_platform_dirs = tools/trace-malloc/lib
endif

ifdef MOZ_TREE_FREETYPE
tier_platform_dirs += modules/freetype2
endif

tier_platform_dirs += xpcom

ifndef MOZ_NATIVE_ZLIB
tier_platform_dirs += modules/zlib
endif

ifdef MOZ_LIBREG
tier_platform_dirs += modules/libreg
endif

tier_platform_dirs += \
		modules/libpref \
		intl \
		netwerk \
		$(NULL)

ifdef MOZ_AUTH_EXTENSION
tier_platform_dirs += extensions/auth
endif

#
# "external" - 3rd party individual libraries
#

ifndef MOZ_NATIVE_JPEG
tier_platform_dirs	+= jpeg
endif

# Installer needs standalone libjar, hence standalone zlib
ifdef MOZ_INSTALLER
tier_platform_dirs	+= modules/zlib/standalone
endif

ifdef MOZ_UPDATER
ifndef MOZ_NATIVE_BZ2
tier_platform_dirs += modules/libbz2
endif
tier_platform_dirs += modules/libmar
tier_platform_dirs += other-licenses/bsdiff
endif

tier_platform_dirs	+= gfx/qcms

ifeq ($(OS_ARCH),WINCE)
tier_platform_dirs += modules/lib7z
endif

#
# "gecko" - core components
#

ifdef MOZ_IPC
tier_platform_dirs += ipc js/ipc js/jetpack
endif

tier_platform_dirs += \
		js/src/xpconnect \
		intl/chardet \
		$(NULL)

ifdef MOZ_ENABLE_GTK2
ifdef MOZ_X11
tier_platform_dirs     += widget/src/gtkxtbin
endif
endif

tier_platform_dirs	+= \
		modules/libjar \
		db \
		$(NULL)

ifdef MOZ_PERMISSIONS
tier_platform_dirs += \
		extensions/cookie \
		extensions/permissions \
		$(NULL)
endif

ifdef MOZ_STORAGE
tier_platform_dirs += storage
endif

ifdef MOZ_RDF
tier_platform_dirs += rdf
endif

ifdef MOZ_JSDEBUGGER
tier_platform_dirs += js/jsd
endif

ifdef MOZ_VORBIS
tier_platform_dirs += \
		media/libvorbis \
		$(NULL)
endif

ifdef MOZ_WEBM
tier_platform_dirs += \
		media/libnestegg \
		media/libvpx \
		$(NULL)
endif

ifdef MOZ_OGG
tier_platform_dirs += \
		media/libogg \
		media/libtheora \
		$(NULL)
endif

ifdef MOZ_SYDNEYAUDIO
tier_platform_dirs += \
		media/libsydneyaudio \
		$(NULL)
endif

tier_platform_dirs	+= \
		uriloader \
		modules/libimg \
		caps \
		parser \
		gfx \
		modules/libpr0n \
		modules/plugin \
		dom \
		view \
		widget \
		content \
		editor \
		layout \
		docshell \
		webshell \
		embedding \
		xpfe/appshell \
		$(NULL)

ifdef MOZ_UNIVERSALCHARDET
tier_platform_dirs += extensions/universalchardet
endif

ifdef ACCESSIBILITY
tier_platform_dirs    += accessible
endif

#
# "toolkit" - xpfe & toolkit
#

tier_platform_dirs += profile

# This must preceed xpfe
ifdef MOZ_JPROF
tier_platform_dirs        += tools/jprof
endif

tier_platform_dirs	+= xpfe/components

ifdef MOZ_ENABLE_XREMOTE
tier_platform_dirs += widget/src/xremoteclient
endif

ifdef MOZ_SPELLCHECK
tier_platform_dirs	+= extensions/spellcheck
endif

tier_platform_dirs	+= toolkit

ifdef MOZ_PSM
tier_platform_dirs	+= security/manager
else
tier_platform_dirs	+= security/manager/boot/public security/manager/ssl/public
endif

ifdef MOZ_PREF_EXTENSIONS
tier_platform_dirs += extensions/pref
endif

# JavaXPCOM JNI code is compiled into libXUL
ifdef MOZ_JAVAXPCOM
tier_platform_dirs += extensions/java/xpcom/src
endif

ifndef BUILD_STATIC_LIBS
ifneq (,$(MOZ_ENABLE_GTK2))
tier_platform_dirs += embedding/browser/gtk
endif
endif

ifndef BUILD_STATIC_LIBS
tier_platform_dirs += toolkit/library
endif

ifdef MOZ_ENABLE_LIBXUL
tier_platform_dirs += xpcom/stub
endif

ifdef NS_TRACE_MALLOC
tier_platform_dirs += tools/trace-malloc
endif

ifdef MOZ_ENABLE_GNOME_COMPONENT
tier_platform_dirs    += toolkit/system/gnome
endif

ifndef MOZ_ENABLE_LIBCONIC
# if libconic is present, it will do its own network monitoring
ifdef MOZ_ENABLE_DBUS
tier_platform_dirs    += toolkit/system/dbus
endif
endif

ifdef MOZ_LEAKY
tier_platform_dirs        += tools/leaky
endif

ifdef MOZ_MAPINFO
tier_platform_dirs	+= tools/codesighs
endif

ifdef MOZ_SERVICES_SYNC
tier_platform_dirs += services/crypto
tier_platform_dirs += services/sync
endif

ifdef ENABLE_TESTS
tier_platform_dirs += testing/mochitest
tier_platform_dirs += testing/xpcshell 
tier_platform_dirs += testing/mozmill
tier_platform_dirs += testing/tools/screenshot
endif

