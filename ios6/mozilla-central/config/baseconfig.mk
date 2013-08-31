INCLUDED_AUTOCONF_MK = 1

includedir := $(includedir)/$(MOZ_APP_NAME)-$(MOZ_APP_VERSION)
idldir = $(datadir)/idl/$(MOZ_APP_NAME)-$(MOZ_APP_VERSION)
installdir = $(libdir)/$(MOZ_APP_NAME)-$(MOZ_APP_VERSION)
sdkdir = $(libdir)/$(MOZ_APP_NAME)-devel-$(MOZ_APP_VERSION)
DIST = $(DEPTH)/dist

# We do magic with OBJ_SUFFIX in config.mk, the following ensures we don't
# manually use it before config.mk inclusion
_OBJ_SUFFIX := $(OBJ_SUFFIX)
OBJ_SUFFIX = $(error config/config.mk needs to be included before using OBJ_SUFFIX)

# We only want to do the pymake sanity on Windows, other os's can cope
ifeq ($(HOST_OS_ARCH),WINNT)
# Ensure invariants between GNU Make and pymake
# Checked here since we want the sane error in a file that
# actually can be found regardless of path-style.
ifeq (_:,$(.PYMAKE)_$(findstring :,$(srcdir)))
$(error Windows-style srcdir being used with GNU make. Did you mean to run $(topsrcdir)/build/pymake/make.py instead? [see-also:     https://developer.mozilla.org/en/Gmake_vs._Pymake])
endif
ifeq (1_a,$(.PYMAKE)_$(firstword a$(subst /, ,$(srcdir))))
$(error MSYS-style srcdir being used with Pymake. Did you mean to run GNU Make instead? [see-also: https://developer.mozilla.org/    en/Gmake_vs._Pymake])
endif
endif # WINNT
