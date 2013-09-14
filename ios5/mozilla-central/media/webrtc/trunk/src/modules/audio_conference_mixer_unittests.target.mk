# This file is generated by gyp; do not edit.

TOOLSET := target
TARGET := audio_conference_mixer_unittests
DEFS_Debug := '-D_FILE_OFFSET_BITS=64' \
	'-DCHROMIUM_BUILD' \
	'-DUSE_NSS=1' \
	'-DTOOLKIT_USES_GTK=1' \
	'-DGTK_DISABLE_SINGLE_INCLUDES=1' \
	'-DENABLE_REMOTING=1' \
	'-DENABLE_P2P_APIS=1' \
	'-DENABLE_CONFIGURATION_POLICY' \
	'-DENABLE_INPUT_SPEECH' \
	'-DENABLE_NOTIFICATIONS' \
	'-DENABLE_GPU=1' \
	'-DENABLE_EGLIMAGE=1' \
	'-DUSE_SKIA=1' \
	'-DENABLE_REGISTER_PROTOCOL_HANDLER=1' \
	'-DENABLE_WEB_INTENTS=1' \
	'-DENABLE_PLUGIN_INSTALLATION=1' \
	'-DWEBRTC_TARGET_PC' \
	'-DWEBRTC_LINUX' \
	'-DWEBRTC_THREAD_RR' \
	'-DUNIT_TEST' \
	'-DGTEST_HAS_RTTI=0' \
	'-D__STDC_FORMAT_MACROS' \
	'-DDYNAMIC_ANNOTATIONS_ENABLED=1' \
	'-DWTF_USE_DYNAMIC_ANNOTATIONS=1' \
	'-D_DEBUG'

# Flags passed to all source files.
CFLAGS_Debug := -Werror \
	-pthread \
	-fno-exceptions \
	-fno-strict-aliasing \
	-Wall \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-fvisibility=hidden \
	-pipe \
	-fPIC \
	-Wextra \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-O0 \
	-g

# Flags passed to only C files.
CFLAGS_C_Debug := 

# Flags passed to only C++ files.
CFLAGS_CC_Debug := -fno-rtti \
	-fno-threadsafe-statics \
	-fvisibility-inlines-hidden \
	-Wsign-compare

INCS_Debug := -Isrc \
	-I. \
	-Itest \
	-Isrc/modules/audio_conference_mixer/interface \
	-Isrc/modules/interface \
	-Itesting/gtest/include \
	-Isrc/system_wrappers/interface

DEFS_Release := '-D_FILE_OFFSET_BITS=64' \
	'-DCHROMIUM_BUILD' \
	'-DUSE_NSS=1' \
	'-DTOOLKIT_USES_GTK=1' \
	'-DGTK_DISABLE_SINGLE_INCLUDES=1' \
	'-DENABLE_REMOTING=1' \
	'-DENABLE_P2P_APIS=1' \
	'-DENABLE_CONFIGURATION_POLICY' \
	'-DENABLE_INPUT_SPEECH' \
	'-DENABLE_NOTIFICATIONS' \
	'-DENABLE_GPU=1' \
	'-DENABLE_EGLIMAGE=1' \
	'-DUSE_SKIA=1' \
	'-DENABLE_REGISTER_PROTOCOL_HANDLER=1' \
	'-DENABLE_WEB_INTENTS=1' \
	'-DENABLE_PLUGIN_INSTALLATION=1' \
	'-DWEBRTC_TARGET_PC' \
	'-DWEBRTC_LINUX' \
	'-DWEBRTC_THREAD_RR' \
	'-DUNIT_TEST' \
	'-DGTEST_HAS_RTTI=0' \
	'-D__STDC_FORMAT_MACROS' \
	'-DNDEBUG' \
	'-DNVALGRIND' \
	'-DDYNAMIC_ANNOTATIONS_ENABLED=0'

# Flags passed to all source files.
CFLAGS_Release := -Werror \
	-pthread \
	-fno-exceptions \
	-fno-strict-aliasing \
	-Wall \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-fvisibility=hidden \
	-pipe \
	-fPIC \
	-Wextra \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-O2 \
	-fno-ident \
	-fdata-sections \
	-ffunction-sections

# Flags passed to only C files.
CFLAGS_C_Release := 

# Flags passed to only C++ files.
CFLAGS_CC_Release := -fno-rtti \
	-fno-threadsafe-statics \
	-fvisibility-inlines-hidden \
	-Wsign-compare

INCS_Release := -Isrc \
	-I. \
	-Itest \
	-Isrc/modules/audio_conference_mixer/interface \
	-Isrc/modules/interface \
	-Itesting/gtest/include \
	-Isrc/system_wrappers/interface

OBJS := $(obj).target/$(TARGET)/src/modules/audio_conference_mixer/source/audio_conference_mixer_unittest.o

# Add to the list of files we specially track dependencies for.
all_deps += $(OBJS)

# Make sure our dependencies are built before any of us.
$(OBJS): | $(obj).target/src/modules/libaudio_conference_mixer.a $(obj).target/testing/libgtest.a $(obj).target/test/libtest_support_main.a $(obj).target/src/system_wrappers/source/libsystem_wrappers.a $(obj).target/src/modules/libaudio_processing.a $(obj).target/src/modules/libaec.a $(obj).target/src/modules/libapm_util.a $(obj).target/src/common_audio/libsignal_processing.a $(obj).target/src/modules/libaec_sse2.a $(obj).target/src/modules/libaecm.a $(obj).target/src/modules/libagc.a $(obj).target/src/common_audio/libvad.a $(obj).target/src/modules/libns.a $(obj).target/src/modules/libaudioproc_debug_proto.a $(obj).target/third_party/protobuf/libprotobuf_lite.a $(obj).target/testing/gtest_prod.stamp $(obj).target/test/libtest_support.a $(obj).target/testing/libgmock.a

# CFLAGS et al overrides must be target-local.
# See "Target-specific Variable Values" in the GNU Make manual.
$(OBJS): TOOLSET := $(TOOLSET)
$(OBJS): GYP_CFLAGS := $(DEFS_$(BUILDTYPE)) $(INCS_$(BUILDTYPE))  $(CFLAGS_$(BUILDTYPE)) $(CFLAGS_C_$(BUILDTYPE))
$(OBJS): GYP_CXXFLAGS := $(DEFS_$(BUILDTYPE)) $(INCS_$(BUILDTYPE))  $(CFLAGS_$(BUILDTYPE)) $(CFLAGS_CC_$(BUILDTYPE))

# Suffix rules, putting all outputs into $(obj).

$(obj).$(TOOLSET)/$(TARGET)/%.o: $(srcdir)/%.cc FORCE_DO_CMD
	@$(call do_cmd,cxx,1)

# Try building from generated source, too.

$(obj).$(TOOLSET)/$(TARGET)/%.o: $(obj).$(TOOLSET)/%.cc FORCE_DO_CMD
	@$(call do_cmd,cxx,1)

$(obj).$(TOOLSET)/$(TARGET)/%.o: $(obj)/%.cc FORCE_DO_CMD
	@$(call do_cmd,cxx,1)

# End of this set of suffix rules
### Rules for final target.
LDFLAGS_Debug := -pthread \
	-Wl,-z,noexecstack \
	-fPIC \
	-B$(builddir)/../../third_party/gold

LDFLAGS_Release := -pthread \
	-Wl,-z,noexecstack \
	-fPIC \
	-B$(builddir)/../../third_party/gold \
	-Wl,-O1 \
	-Wl,--as-needed \
	-Wl,--gc-sections

LIBS := -lrt

$(builddir)/audio_conference_mixer_unittests: GYP_LDFLAGS := $(LDFLAGS_$(BUILDTYPE))
$(builddir)/audio_conference_mixer_unittests: LIBS := $(LIBS)
$(builddir)/audio_conference_mixer_unittests: LD_INPUTS := $(OBJS) $(obj).target/src/modules/libaudio_conference_mixer.a $(obj).target/testing/libgtest.a $(obj).target/test/libtest_support_main.a $(obj).target/src/system_wrappers/source/libsystem_wrappers.a $(obj).target/src/modules/libaudio_processing.a $(obj).target/src/modules/libaec.a $(obj).target/src/modules/libapm_util.a $(obj).target/src/common_audio/libsignal_processing.a $(obj).target/src/modules/libaec_sse2.a $(obj).target/src/modules/libaecm.a $(obj).target/src/modules/libagc.a $(obj).target/src/common_audio/libvad.a $(obj).target/src/modules/libns.a $(obj).target/src/modules/libaudioproc_debug_proto.a $(obj).target/third_party/protobuf/libprotobuf_lite.a $(obj).target/test/libtest_support.a $(obj).target/testing/libgmock.a
$(builddir)/audio_conference_mixer_unittests: TOOLSET := $(TOOLSET)
$(builddir)/audio_conference_mixer_unittests: $(OBJS) $(obj).target/src/modules/libaudio_conference_mixer.a $(obj).target/testing/libgtest.a $(obj).target/test/libtest_support_main.a $(obj).target/src/system_wrappers/source/libsystem_wrappers.a $(obj).target/src/modules/libaudio_processing.a $(obj).target/src/modules/libaec.a $(obj).target/src/modules/libapm_util.a $(obj).target/src/common_audio/libsignal_processing.a $(obj).target/src/modules/libaec_sse2.a $(obj).target/src/modules/libaecm.a $(obj).target/src/modules/libagc.a $(obj).target/src/common_audio/libvad.a $(obj).target/src/modules/libns.a $(obj).target/src/modules/libaudioproc_debug_proto.a $(obj).target/third_party/protobuf/libprotobuf_lite.a $(obj).target/test/libtest_support.a $(obj).target/testing/libgmock.a FORCE_DO_CMD
	$(call do_cmd,link)

all_deps += $(builddir)/audio_conference_mixer_unittests
# Add target alias
.PHONY: audio_conference_mixer_unittests
audio_conference_mixer_unittests: $(builddir)/audio_conference_mixer_unittests
