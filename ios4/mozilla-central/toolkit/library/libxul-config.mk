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
# The Original Code is Mozilla libxul
#
# The Initial Developer of the Original Code is
# Benjamin Smedberg <benjamin@smedbergs.us>
#
# Portions created by the Initial Developer are Copyright (C) 2005
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Shawn Wilsher <me@shawnwilsher.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either of the GNU General Public License Version 2 or later (the "GPL"),
# or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

CPPSRCS += \
	nsStaticXULComponents.cpp \
	$(NULL)

ifeq (,$(filter-out WINCE WINNT,$(OS_ARCH)))
REQUIRES += widget gfx
CPPSRCS += \
	nsDllMain.cpp \
	$(NULL)
endif

ifeq ($(OS_ARCH)_$(GNU_CC),WINNT_)
CPPSRCS += \
	dlldeps.cpp \
	nsGFXDeps.cpp \
	$(NULL)

RCINCLUDE = xulrunner.rc

ifndef MOZ_NATIVE_ZLIB
CPPSRCS += dlldeps-zlib.cpp
DEFINES += -DZLIB_INTERNAL
endif

LOCAL_INCLUDES += -I$(topsrcdir)/widget/src/windows
endif

ifneq (,$(filter WINNT OS2,$(OS_ARCH)))
REQUIRES += libreg
DEFINES	+= -DZLIB_DLL=1
endif

ifeq ($(OS_ARCH),OS2)
REQUIRES += widget gfx

CPPSRCS += \
	dlldeps.cpp \
	nsGFXDeps.cpp \
	$(NULL)

ifndef MOZ_NATIVE_ZLIB
CPPSRCS += dlldeps-zlib.cpp
DEFINES += -DZLIB_INTERNAL
endif

ifdef MOZ_ENABLE_LIBXUL
RESFILE = xulrunos2.res
RCFLAGS += -i $(topsrcdir)/widget/src/os2
endif

LOCAL_INCLUDES += -I$(topsrcdir)/widget/src/os2
endif

# dependent libraries
ifdef MOZ_IPC
STATIC_LIBS += \
  jsipc_s \
  domipc_s \
  domplugins_s \
  mozipc_s \
  mozipdlgen_s \
  ipcshell_s \
  gfxipc_s \
  $(NULL)

ifdef MOZ_IPDL_TESTS
STATIC_LIBS += ipdlunittest_s
endif

ifeq (Linux,$(OS_ARCH))
ifneq (Android,$(OS_TARGET))
OS_LIBS += -lrt
endif
endif
ifeq (WINNT,$(OS_ARCH))
OS_LIBS += dbghelp.lib
endif
endif

STATIC_LIBS += \
	xpcom_core \
	ucvutil_s \
	gkgfx \
	$(NULL)

ifdef MOZ_IPC
STATIC_LIBS += chromium_s
endif

ifdef MOZ_LIBREG
STATIC_LIBS += \
	mozreg_s \
	$(NULL)
endif

# component libraries
COMPONENT_LIBS += \
	necko \
	uconv \
	i18n \
	chardet \
	jar$(VERSION_NUMBER) \
	pref \
	htmlpars \
	imglib2 \
	gklayout \
	docshell \
	embedcomponents \
	webbrwsr \
	nsappshell \
	txmgr \
	commandlines \
	toolkitcomps \
	pipboot \
	pipnss \
	appcomps \
	$(NULL)

ifdef MOZ_IPC
COMPONENT_LIBS +=  jetpack_s
endif

ifdef BUILD_CTYPES
COMPONENT_LIBS += \
	jsctypes \
	$(NULL)
endif

COMPONENT_LIBS += jsperf

ifdef MOZ_PLUGINS
DEFINES += -DMOZ_PLUGINS
COMPONENT_LIBS += \
	gkplugin \
	$(NULL)
endif

ifdef MOZ_XUL
ifdef MOZ_ENABLE_GTK2
COMPONENT_LIBS += \
	unixproxy \
	$(NULL)
endif
endif

ifneq (,$(filter cocoa,$(MOZ_WIDGET_TOOLKIT)))
COMPONENT_LIBS += \
	osxproxy \
	$(NULL)
endif

ifdef MOZ_XUL
ifeq (qt,$(MOZ_WIDGET_TOOLKIT))
COMPONENT_LIBS += \
        unixproxy \
        $(NULL)
endif
endif

ifneq (,$(filter windows,$(MOZ_WIDGET_TOOLKIT)))
COMPONENT_LIBS += \
	windowsproxy \
	$(NULL)
endif

ifdef MOZ_JSDEBUGGER
DEFINES += -DMOZ_JSDEBUGGER
COMPONENT_LIBS += \
	jsd \
	$(NULL)
endif

ifdef MOZ_PREF_EXTENSIONS
DEFINES += -DMOZ_PREF_EXTENSIONS
COMPONENT_LIBS += \
	autoconfig \
	$(NULL)
endif

ifdef MOZ_AUTH_EXTENSION
COMPONENT_LIBS += auth
DEFINES += -DMOZ_AUTH_EXTENSION
endif

ifdef MOZ_PERMISSIONS
COMPONENT_LIBS += cookie permissions
DEFINES += -DMOZ_PERMISSIONS
endif

ifdef MOZ_UNIVERSALCHARDET
COMPONENT_LIBS += universalchardet
DEFINES += -DMOZ_UNIVERSALCHARDET
endif

ifndef MOZ_PLAINTEXT_EDITOR_ONLY
COMPONENT_LIBS += composer
else
DEFINES += -DMOZ_PLAINTEXT_EDITOR_ONLY
endif

ifdef MOZ_RDF
COMPONENT_LIBS += \
	rdf \
	windowds \
	$(NULL)
endif

ifeq (,$(filter qt beos os2 cocoa windows,$(MOZ_WIDGET_TOOLKIT)))
ifdef MOZ_XUL
COMPONENT_LIBS += fileview
DEFINES += -DMOZ_FILEVIEW
endif
endif

ifdef MOZ_STORAGE
COMPONENT_LIBS += storagecomps
EXTRA_DSO_LDOPTS += $(SQLITE_LIBS)
endif

ifdef MOZ_PLACES
STATIC_LIBS += morkreader_s

COMPONENT_LIBS += \
	places \
	$(NULL)
else
ifdef MOZ_MORK
ifdef MOZ_XUL
COMPONENT_LIBS += \
	mork \
	$(NULL)
endif
endif
endif

ifdef MOZ_XUL
COMPONENT_LIBS += \
	tkautocomplete \
	satchel \
	pippki \
	$(NULL)
endif

ifdef MOZ_ENABLE_GTK2
COMPONENT_LIBS += widget_gtk2
ifdef MOZ_PREF_EXTENSIONS
COMPONENT_LIBS += system-pref
endif
endif

ifdef MOZ_ENABLE_GTK2
ifdef MOZ_X11
STATIC_LIBS += gtkxtbin
endif
endif

# Platform-specific icon channel stuff - supported mostly-everywhere
ifneq (,$(filter beos windows os2 mac cocoa gtk2 qt,$(MOZ_WIDGET_TOOLKIT)))
DEFINES += -DICON_DECODER
COMPONENT_LIBS += imgicon
endif

ifeq ($(MOZ_WIDGET_TOOLKIT),android)
COMPONENT_LIBS += widget_android
endif

STATIC_LIBS += thebes ycbcr

ifneq ($(OS_ARCH)_$(OS_TEST),Linux_x86_64)
STATIC_LIBS += angle
endif

COMPONENT_LIBS += gkgfxthebes

ifeq (windows,$(MOZ_WIDGET_TOOLKIT))
COMPONENT_LIBS += gkwidget
endif
ifeq (beos,$(MOZ_WIDGET_TOOLKIT))
COMPONENT_LIBS += widget_beos
endif
ifeq (os2,$(MOZ_WIDGET_TOOLKIT))
COMPONENT_LIBS += wdgtos2
endif
ifeq (cocoa,$(MOZ_WIDGET_TOOLKIT))
COMPONENT_LIBS += widget_mac
endif
ifeq (qt,$(MOZ_WIDGET_TOOLKIT))
COMPONENT_LIBS += widget_qt
endif
ifeq (uikit,$(MOZ_WIDGET_TOOLKIT))
COMPONENT_LIBS += widget_uikit
endif

ifdef ACCESSIBILITY
COMPONENT_LIBS += accessibility
endif

ifdef MOZ_ENABLE_XREMOTE
COMPONENT_LIBS += remoteservice
endif

ifdef MOZ_SPELLCHECK
DEFINES += -DMOZ_SPELLCHECK
COMPONENT_LIBS += spellchecker
endif

ifdef MOZ_ZIPWRITER
DEFINES += -DMOZ_ZIPWRITER
COMPONENT_LIBS += zipwriter
endif

ifdef MOZ_DEBUG
ifdef ENABLE_TESTS
COMPONENT_LIBS += gkdebug
endif
endif

ifeq ($(MOZ_WIDGET_TOOLKIT),cocoa)
OS_LIBS += -framework OpenGL -lcups
endif

EXTRA_DSO_LDOPTS += \
	$(LIBS_DIR) \
	$(JPEG_LIBS) \
	$(PNG_LIBS) \
	$(QCMS_LIBS) \
	$(MOZ_JS_LIBS) \
	$(NSS_LIBS) \
	$(MOZ_CAIRO_LIBS) \
	$(MOZ_HARFBUZZ_LIBS) \
	$(NULL)

ifdef MOZ_NATIVE_ZLIB
EXTRA_DSO_LDOPTS += $(ZLIB_LIBS)
else
EXTRA_DSO_LDOPTS += $(MOZ_ZLIB_LIBS)
endif

ifdef MOZ_NATIVE_HUNSPELL
EXTRA_DSO_LDOPTS += $(MOZ_HUNSPELL_LIBS)
endif

ifdef MOZ_NATIVE_LIBEVENT
EXTRA_DSO_LDOPTS += $(MOZ_LIBEVENT_LIBS)
endif

ifdef MOZ_SYDNEYAUDIO
ifeq ($(OS_ARCH),Linux)
EXTRA_DSO_LDOPTS += $(MOZ_ALSA_LIBS)
endif
endif

ifdef HAVE_CLOCK_MONOTONIC
EXTRA_DSO_LDOPTS += $(REALTIME_LIBS)
endif

ifeq (android,$(MOZ_WIDGET_TOOLKIT))
OS_LIBS += -lGLESv2
endif
