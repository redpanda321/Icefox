# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Ensure JAVA_CLASSPATH and ANDROID_SDK are defined before including this file.
# We use common android defaults for boot class path and java version.
ifndef ANDROID_SDK
  $(error ANDROID_SDK must be defined before including android-common.mk)
endif

ifndef JAVA_CLASSPATH
  $(error JAVA_CLASSPATH must be defined before including android-common.mk)
endif

DX=$(ANDROID_PLATFORM_TOOLS)/dx
AAPT=$(ANDROID_PLATFORM_TOOLS)/aapt
APKBUILDER=$(ANDROID_SDK)/../../tools/apkbuilder
ZIPALIGN=$(ANDROID_SDK)/../../tools/zipalign

ifdef JARSIGNER
  APKBUILDER_FLAGS += -u
endif

# For Android, this defaults to $(ANDROID_SDK)/android.jar
ifndef JAVA_BOOTCLASSPATH
  JAVA_BOOTCLASSPATH = $(ANDROID_SDK)/android.jar:$(ANDROID_COMPAT_LIB)
endif

# For Android, we default to 1.5
ifndef JAVA_VERSION
  JAVA_VERSION = 1.5
endif

JAVAC_FLAGS = \
  -target $(JAVA_VERSION) \
  -source $(JAVA_VERSION) \
  -classpath $(JAVA_CLASSPATH) \
  -bootclasspath $(JAVA_BOOTCLASSPATH) \
  -encoding UTF8 \
  -g:source,lines \
  -Werror \
  $(NULL)
