# vim:set ts=8 sw=8 sts=8 noet:
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
# The Original Code is the Mozilla Browser code.
#
# The Initial Developer of the Original Code is
# Benjamin Smedberg <bsmedberg@covad.net>
# Portions created by the Initial Developer are Copyright (C) 2004
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#  Axel Hecht <l10n@mozilla.com>
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


# Shared makefile that can be used to easily kick off l10n builds
# of Mozilla applications.
# This makefile should be included, and then assumes that the including
# makefile defines the following targets:
# clobber-zip
#   This target should remove all language dependent-files from $(STAGEDIST),
#   depending on $(AB_CD) set to the locale code.
#   $(AB_CD) will be en-US on the initial unpacking of the package
# libs-%
#   This target should call into the various libs targets that this
#   application depends on.
#   Make sure to set BOTH_MANIFESTS=1, as this will be called only once
#   for both packages and language packs.
# installer-%
#   This target should list all required targets, a typical rule would be
#	installers-%: clobber-% langpack-% repackage-zip-%
#		@echo "repackaging done"
#   to initially clobber the locale staging area, and then to build the
#   language pack and zip package.
#   Other targets like windows installers might be listed, too, and should
#   be defined in the including makefile.
# The including makefile should provide values for the variables
#   MOZ_APP_VERSION and MOZ_LANGPACK_EID.


run_for_effects := $(shell if test ! -d $(DIST); then $(NSINSTALL) -D $(DIST); fi)
_ABS_DIST := $(shell cd $(DIST) && pwd)


# This makefile uses variable overrides from the libs-% target to
# build non-default locales to non-default dist/ locations. Be aware!

AB = $(firstword $(subst -, ,$(AB_CD)))

core_abspath = $(if $(findstring :,$(1)),$(1),$(if $(filter /%,$(1)),$(1),$(CURDIR)/$(1)))

# These are defaulted to be compatible with the files the wget-en-US target
# pulls. You may override them if you provide your own files. You _must_
# override them when MOZ_PKG_PRETTYNAMES is defined - the defaults will not
# work in that case.
ZIP_IN ?= $(_ABS_DIST)/$(PACKAGE)
WIN32_INSTALLER_IN ?= $(_ABS_DIST)/$(PKG_INST_PATH)$(PKG_INST_BASENAME).exe

# Allows overriding the final destination of the repackaged file
ZIP_OUT ?= $(_ABS_DIST)/$(PACKAGE)

DEFINES += \
	-DAB_CD=$(AB_CD) \
	-DMOZ_LANGPACK_EID=$(MOZ_LANGPACK_EID) \
	-DMOZ_APP_VERSION=$(MOZ_APP_VERSION) \
	-DLOCALE_SRCDIR=$(call core_abspath,$(LOCALE_SRCDIR)) \
	-DPKG_BASENAME="$(PKG_BASENAME)" \
	-DPKG_INST_BASENAME="$(PKG_INST_BASENAME)" \
	$(NULL)


clobber-%:
	$(RM) -rf $(DIST)/xpi-stage/locale-$*


PACKAGER_NO_LIBS = 1
include $(MOZILLA_DIR)/toolkit/mozapps/installer/packager.mk


ifeq (cocoa,$(MOZ_WIDGET_TOOLKIT))
STAGEDIST = $(_ABS_DIST)/l10n-stage/$(MOZ_PKG_APPNAME)/$(_APPNAME)/Contents/MacOS
else
STAGEDIST = $(_ABS_DIST)/l10n-stage/$(MOZ_PKG_DIR)
endif

$(STAGEDIST): AB_CD:=en-US
$(STAGEDIST): UNPACKAGE=$(ZIP_IN)
$(STAGEDIST): $(ZIP_IN)
# only mac needs to remove the parent of STAGEDIST...
ifeq (cocoa,$(MOZ_WIDGET_TOOLKIT))
	$(RM) -r -v $(DIST)/l10n-stage
else
# ... and windows doesn't like removing STAGEDIST itself, remove all children
	find $(STAGEDIST) -maxdepth 1 -print0 | xargs -0 $(RM) -r
endif
	$(NSINSTALL) -D $(DIST)/l10n-stage
	cd $(DIST)/l10n-stage && \
	  $(UNMAKE_PACKAGE)
	$(MAKE) clobber-zip AB_CD=en-US


unpack: $(STAGEDIST)
	@echo done unpacking

# The path to the object dir for the mozilla-central build system,
# may be overridden if necessary.
MOZDEPTH ?= $(DEPTH)

repackage-zip: UNPACKAGE="$(ZIP_IN)"
repackage-zip:
# call a hook for apps to put their uninstall helper.exe into the package
	$(UNINSTALLER_PACKAGE_HOOK)
# copy xpi-stage over, but not install.rdf and chrome.manifest,
# those are just for language packs
	cd $(DIST)/xpi-stage/locale-$(AB_CD) && \
	  tar --exclude=install.rdf --exclude=chrome.manifest $(TAR_CREATE_FLAGS) - * | ( cd $(STAGEDIST) && tar -xf - )
	mv $(STAGEDIST)/chrome/$(AB_CD).manifest $(STAGEDIST)/chrome/localized.manifest
ifneq (en,$(AB))
ifeq (cocoa,$(MOZ_WIDGET_TOOLKIT))
	mv $(_ABS_DIST)/l10n-stage/$(MOZ_PKG_APPNAME)/$(_APPNAME)/Contents/Resources/en.lproj $(_ABS_DIST)/l10n-stage/$(MOZ_PKG_APPNAME)/$(_APPNAME)/Contents/Resources/$(AB).lproj
endif
endif
	$(NSINSTALL) -D $(DIST)/l10n-stage/$(PKG_PATH)
	cd $(DIST)/l10n-stage; \
	  $(MAKE_PACKAGE)
ifeq (WINCE,$(OS_ARCH))
	cd $(DIST)/l10n-stage; \
	  $(MAKE_CAB)
endif
ifdef MOZ_MAKE_COMPLETE_MAR
	$(MAKE) -C $(MOZDEPTH)/tools/update-packaging full-update AB_CD=$(AB_CD) \
	  MOZ_PKG_PRETTYNAMES=$(MOZ_PKG_PRETTYNAMES) \
	  PACKAGE_BASE_DIR="$(_ABS_DIST)/l10n-stage" \
	  DIST="$(_ABS_DIST)"
endif
# packaging done, undo l10n stuff
ifneq (en,$(AB))
ifeq (cocoa,$(MOZ_WIDGET_TOOLKIT))
	mv $(_ABS_DIST)/l10n-stage/$(MOZ_PKG_APPNAME)/$(_APPNAME)/Contents/Resources/$(AB).lproj $(_ABS_DIST)/l10n-stage/$(MOZ_PKG_APPNAME)/$(_APPNAME)/Contents/Resources/en.lproj
endif
endif
ifdef MOZ_OMNIJAR
	@(cd $(STAGEDIST) && $(UNPACK_OMNIJAR))
endif
	$(MAKE) clobber-zip AB_CD=$(AB_CD)
	$(NSINSTALL) -D $(DIST)/$(PKG_PATH)
	mv -f "$(DIST)/l10n-stage/$(PACKAGE)" "$(ZIP_OUT)"
ifeq (WINCE,$(OS_ARCH))
	mv -f "$(DIST)/l10n-stage/$(PKG_BASENAME).cab" "$(DIST)/$(PKG_PATH)$(PKG_BASENAME).cab"
endif

repackage-zip-%: $(ZIP_IN) $(STAGEDIST) libs-%
	@$(MAKE) repackage-zip AB_CD=$* ZIP_IN=$(ZIP_IN)

APP_DEFINES = $(firstword $(wildcard $(LOCALE_SRCDIR)/defines.inc) \
                          $(srcdir)/en-US/defines.inc)
TK_DEFINES = $(firstword \
   $(wildcard $(call EXPAND_LOCALE_SRCDIR,toolkit/locales)/defines.inc) \
   $(MOZILLA_DIR)/toolkit/locales/en-US/defines.inc)

langpack-%: LANGPACK_FILE=$(_ABS_DIST)/$(PKG_LANGPACK_PATH)$(PKG_LANGPACK_BASENAME).xpi
langpack-%: AB_CD=$*
langpack-%: XPI_NAME=locale-$*
langpack-%: libs-%
	@echo "Making langpack $(LANGPACK_FILE)"
	$(NSINSTALL) -D $(DIST)/$(PKG_LANGPACK_PATH)
	$(PERL) $(MOZILLA_DIR)/config/preprocessor.pl $(DEFINES) $(ACDEFINES) -I$(TK_DEFINES) -I$(APP_DEFINES) $(srcdir)/generic/install.rdf > $(FINAL_TARGET)/install.rdf
	cd $(DIST)/xpi-stage/locale-$(AB_CD) && \
	  $(ZIP) -r9D $(LANGPACK_FILE) install.rdf chrome chrome.manifest -x chrome/$(AB_CD).manifest


# This variable is to allow the wget-en-US target to know which ftp server to download from
ifndef EN_US_BINARY_URL 
EN_US_BINARY_URL = $(error You must set EN_US_BINARY_URL)
endif

# This make target allows us to wget the latest en-US binary from a specified website
# The make installers-% target needs the en-US binary in dist/
# and for the windows repackages we need the .installer.exe in dist/sea
wget-en-US:
ifndef WGET
	$(error Wget not installed)
endif
	(cd $(_ABS_DIST) && $(WGET) -nv -N  $(EN_US_BINARY_URL)/$(PACKAGE))
	@echo "Downloaded $(EN_US_BINARY_URL)/$(PACKAGE) to $(_ABS_DIST)/$(PACKAGE)"
ifdef RETRIEVE_WINDOWS_INSTALLER
ifeq ($(OS_ARCH), WINNT)
	$(NSINSTALL) -D $(_ABS_DIST)/$(PKG_INST_PATH)
	(cd $(_ABS_DIST)/$(PKG_INST_PATH) && $(WGET) -nv -N "$(EN_US_BINARY_URL)/$(PKG_PATH)$(PKG_INST_BASENAME).exe")
	@echo "Downloaded $(EN_US_BINARY_URL)/$(PKG_PATH)$(PKG_INST_BASENAME).exe to $(_ABS_DIST)/$(PKG_INST_PATH)$(PKG_INST_BASENAME).exe"
endif
endif

generate-snippet-%:
	$(PYTHON) $(MOZILLA_DIR)/tools/update-packaging/generatesnippet.py \
          --mar-path=$(_ABS_DIST)/update \
          --application-ini-file=$(STAGEDIST)/application.ini \
          --locale=$* \
          --product=$(MOZ_PKG_APPNAME) \
          --platform=$(MOZ_PKG_PLATFORM) \
          --download-base-URL=$(DOWNLOAD_BASE_URL) \
          --verbose
