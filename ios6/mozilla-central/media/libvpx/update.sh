#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

if [ $# -lt 1 ]; then
  echo Usage: update.sh /path/to/libvpx/
  echo The libvpx dir must contain a directory "objdir" with the following directories configured in it:
  echo   * objdir/x86-win32-vs8
  echo   * objdir/x86-linux-gcc
  echo   * objdir/generic-gnu
  echo   * objdir/x86-darwin9-gcc
  echo   * objdir/x86_64-darwin9-gcc
  echo   * objdir/armv7-linux-gcc
  echo You can configure these from objdir/$target with the following command:
  echo $ ../../configure --target=$target --disable-vp8-encoder --disable-examples --disable-install-docs
  echo On Mac, you also need --enable-pic
  exit -1
fi

# These are relative to SDK source dir.
commonFiles=(
  vp8/vp8_cx_iface.c
  vp8/vp8_dx_iface.c
  vp8/common/alloccommon.c
  vp8/common/asm_com_offsets.c
  vp8/common/blockd.c
  vp8/common/debugmodes.c
  vp8/common/dequantize.c
  vp8/common/entropy.c
  vp8/common/entropymode.c
  vp8/common/entropymv.c
  vp8/common/extend.c
  vp8/common/filter.c
  vp8/common/findnearmv.c
  vp8/common/idct_blk.c
  vp8/common/idctllm.c
  vp8/common/loopfilter.c
  vp8/common/loopfilter_filters.c
  vp8/common/mbpitch.c
  vp8/common/modecont.c
  vp8/common/modecontext.c
  vp8/common/postproc.c
  vp8/common/quant_common.c
  vp8/common/reconinter.c
  vp8/common/reconintra.c
  vp8/common/reconintra4x4.c
  vp8/common/setupintrarecon.c
  vp8/common/swapyv12buffer.c
  vp8/common/treecoder.c
  vp8/common/arm/arm_systemdependent.c
  vp8/common/arm/bilinearfilter_arm.c
  vp8/common/arm/dequantize_arm.c
  vp8/common/arm/filter_arm.c
  vp8/common/arm/loopfilter_arm.c
  vp8/common/arm/reconintra_arm.c
  vp8/common/arm/armv6/idct_blk_v6.c
  vp8/common/arm/neon/idct_blk_neon.c
  vp8/common/generic/systemdependent.c
  vp8/common/x86/filter_x86.c
  vp8/common/x86/idct_blk_mmx.c
  vp8/common/x86/idct_blk_sse2.c
  vp8/common/x86/loopfilter_x86.c
  vp8/common/x86/recon_wrapper_sse2.c
  vp8/common/x86/vp8_asm_stubs.c
  vp8/common/x86/x86_systemdependent.c
  vp8/decoder/asm_dec_offsets.c
  vp8/decoder/dboolhuff.c
  vp8/decoder/decodemv.c
  vp8/decoder/decodframe.c
  vp8/decoder/detokenize.c
  vp8/decoder/error_concealment.c
  vp8/decoder/onyxd_if.c
  vp8/decoder/reconintra_mt.c
  vp8/decoder/threading.c
  vp8/decoder/arm/arm_dsystemdependent.c
  vp8/decoder/generic/dsystemdependent.c
  vp8/decoder/x86/x86_dsystemdependent.c
  vp8/encoder/asm_enc_offsets.c
  vp8/encoder/bitstream.c
  vp8/encoder/boolhuff.c
  vp8/encoder/dct.c
  vp8/encoder/encodeframe.c
  vp8/encoder/encodeintra.c
  vp8/encoder/encodemb.c
  vp8/encoder/encodemv.c
  vp8/encoder/ethreading.c
  vp8/encoder/firstpass.c
  vp8/encoder/lookahead.c
  vp8/encoder/mcomp.c
  vp8/encoder/modecosts.c
  vp8/encoder/mr_dissim.c
  vp8/encoder/onyx_if.c
  vp8/encoder/pickinter.c
  vp8/encoder/picklpf.c
  vp8/encoder/psnr.c
  vp8/encoder/quantize.c
  vp8/encoder/ratectrl.c
  vp8/encoder/rdopt.c
  vp8/encoder/sad_c.c
  vp8/encoder/segmentation.c
  vp8/encoder/temporal_filter.c
  vp8/encoder/tokenize.c
  vp8/encoder/treewriter.c
  vp8/encoder/variance_c.c
  vp8/encoder/arm/arm_csystemdependent.c
  vp8/encoder/arm/boolhuff_arm.c
  vp8/encoder/arm/dct_arm.c
  vp8/encoder/arm/quantize_arm.c
  vp8/encoder/arm/variance_arm.c
  vp8/encoder/arm/neon/picklpf_arm.c
  vp8/encoder/generic/csystemdependent.c
  vp8/encoder/x86/variance_mmx.c
  vp8/encoder/x86/variance_sse2.c
  vp8/encoder/x86/variance_ssse3.c
  vp8/encoder/x86/x86_csystemdependent.c
  vpx/src/vpx_codec.c
  vpx/src/vpx_decoder.c
  vpx/src/vpx_decoder_compat.c
  vpx/src/vpx_encoder.c
  vpx/src/vpx_image.c
  vpx_mem/vpx_mem.c
  vpx_ports/arm_cpudetect.c
  vpx_scale/arm/neon/yv12extend_arm.c
  vpx_scale/generic/gen_scalers.c
  vpx_scale/generic/scalesystemdependent.c
  vpx_scale/generic/vpxscale.c
  vpx_scale/generic/yv12config.c
  vpx_scale/generic/yv12extend.c
  vp8/common/alloccommon.h
  vp8/common/blockd.h
  vp8/common/coefupdateprobs.h
  vp8/common/common.h
  vp8/common/default_coef_probs.h
  vp8/common/dequantize.h
  vp8/common/entropy.h
  vp8/common/entropymode.h
  vp8/common/entropymv.h
  vp8/common/extend.h
  vp8/common/filter.h
  vp8/common/findnearmv.h
  vp8/common/header.h
  vp8/common/idct.h
  vp8/common/invtrans.h
  vp8/common/loopfilter.h
  vp8/common/modecont.h
  vp8/common/mv.h
  vp8/common/onyx.h
  vp8/common/onyxc_int.h
  vp8/common/onyxd.h
  vp8/common/postproc.h
  vp8/common/ppflags.h
  vp8/common/pragmas.h
  vp8/common/quant_common.h
  vp8/common/recon.h
  vp8/common/reconinter.h
  vp8/common/reconintra.h
  vp8/common/reconintra4x4.h
  vp8/common/setupintrarecon.h
  vp8/common/subpixel.h
  vp8/common/swapyv12buffer.h
  vp8/common/systemdependent.h
  vp8/common/threading.h
  vp8/common/treecoder.h
  vp8/common/arm/bilinearfilter_arm.h
  vp8/common/arm/dequantize_arm.h
  vp8/common/arm/idct_arm.h
  vp8/common/arm/loopfilter_arm.h
  vp8/common/arm/recon_arm.h
  vp8/common/arm/subpixel_arm.h
  vp8/common/x86/dequantize_x86.h
  vp8/common/x86/filter_x86.h
  vp8/common/x86/idct_x86.h
  vp8/common/x86/loopfilter_x86.h
  vp8/common/x86/postproc_x86.h
  vp8/common/x86/recon_x86.h
  vp8/common/x86/subpixel_x86.h
  vp8/decoder/dboolhuff.h
  vp8/decoder/decodemv.h
  vp8/decoder/decoderthreading.h
  vp8/decoder/detokenize.h
  vp8/decoder/ec_types.h
  vp8/decoder/error_concealment.h
  vp8/decoder/onyxd_int.h
  vp8/decoder/reconintra_mt.h
  vp8/decoder/treereader.h
  vp8/encoder/bitstream.h
  vp8/encoder/block.h
  vp8/encoder/boolhuff.h
  vp8/encoder/dct.h
  vp8/encoder/defaultcoefcounts.h
  vp8/encoder/encodeintra.h
  vp8/encoder/encodemb.h
  vp8/encoder/encodemv.h
  vp8/encoder/firstpass.h
  vp8/encoder/lookahead.h
  vp8/encoder/mcomp.h
  vp8/encoder/modecosts.h
  vp8/encoder/mr_dissim.h
  vp8/encoder/onyx_int.h
  vp8/encoder/pickinter.h
  vp8/encoder/psnr.h
  vp8/encoder/quantize.h
  vp8/encoder/ratectrl.h
  vp8/encoder/rdopt.h
  vp8/encoder/segmentation.h
  vp8/encoder/temporal_filter.h
  vp8/encoder/tokenize.h
  vp8/encoder/treewriter.h
  vp8/encoder/variance.h
  vp8/encoder/arm/dct_arm.h
  vp8/encoder/arm/encodemb_arm.h
  vp8/encoder/arm/quantize_arm.h
  vp8/encoder/arm/variance_arm.h
  vp8/encoder/x86/dct_x86.h
  vp8/encoder/x86/encodemb_x86.h
  vp8/encoder/x86/mcomp_x86.h
  vp8/encoder/x86/quantize_x86.h
  vp8/encoder/x86/temporal_filter_x86.h
  vp8/encoder/x86/variance_x86.h
  vpx/internal/vpx_codec_internal.h
  vpx/vp8cx.h
  vpx/vp8dx.h
  vpx/vp8e.h
  vpx/vp8.h
  vpx/vpx_codec.h
  vpx/vpx_codec_impl_bottom.h
  vpx/vpx_codec_impl_top.h
  vpx/vpx_decoder_compat.h
  vpx/vpx_decoder.h
  vpx/vpx_encoder.h
  vpx/vpx_image.h
  vpx/vpx_integer.h
  vpx_mem/include/vpx_mem_intrnl.h
  vpx_mem/vpx_mem.h
  vpx_ports/arm.h
  vpx_ports/asm_offsets.h
  vpx_ports/mem.h
  vpx_ports/vpx_timer.h
  vpx_ports/x86.h
  vpx_scale/scale_mode.h
  vpx_scale/vpxscale.h
  vpx_scale/yv12config.h
  vpx_scale/yv12extend.h
  vpx_scale/arm/yv12extend_arm.h
  vpx_scale/generic/yv12extend_generic.h
  vp8/common/arm/armv6/bilinearfilter_v6.asm
  vp8/common/arm/armv6/copymem16x16_v6.asm
  vp8/common/arm/armv6/copymem8x4_v6.asm
  vp8/common/arm/armv6/copymem8x8_v6.asm
  vp8/common/arm/armv6/dc_only_idct_add_v6.asm
  vp8/common/arm/armv6/dequant_idct_v6.asm
  vp8/common/arm/armv6/dequantize_v6.asm
  vp8/common/arm/armv6/iwalsh_v6.asm
  vp8/common/arm/armv6/filter_v6.asm
  vp8/common/arm/armv6/idct_v6.asm
  vp8/common/arm/armv6/intra4x4_predict_v6.asm
  vp8/common/arm/armv6/iwalsh_v6.asm
  vp8/common/arm/armv6/loopfilter_v6.asm
  vp8/common/arm/armv6/simpleloopfilter_v6.asm
  vp8/common/arm/armv6/sixtappredict8x4_v6.asm
  vp8/common/arm/neon/bilinearpredict16x16_neon.asm
  vp8/common/arm/neon/bilinearpredict4x4_neon.asm
  vp8/common/arm/neon/bilinearpredict8x4_neon.asm
  vp8/common/arm/neon/bilinearpredict8x8_neon.asm
  vp8/common/arm/neon/buildintrapredictorsmby_neon.asm
  vp8/common/arm/neon/copymem16x16_neon.asm
  vp8/common/arm/neon/copymem8x4_neon.asm
  vp8/common/arm/neon/copymem8x8_neon.asm
  vp8/common/arm/neon/dc_only_idct_add_neon.asm
  vp8/common/arm/neon/dequant_idct_neon.asm
  vp8/common/arm/neon/dequantizeb_neon.asm
  vp8/common/arm/neon/idct_dequant_0_2x_neon.asm
  vp8/common/arm/neon/idct_dequant_full_2x_neon.asm
  vp8/common/arm/neon/iwalsh_neon.asm
  vp8/common/arm/neon/loopfilter_neon.asm
  vp8/common/arm/neon/loopfiltersimplehorizontaledge_neon.asm
  vp8/common/arm/neon/loopfiltersimpleverticaledge_neon.asm
  vp8/common/arm/neon/mbloopfilter_neon.asm
  vp8/common/arm/neon/save_neon_reg.asm
  vp8/common/arm/neon/shortidct4x4llm_neon.asm
  vp8/common/arm/neon/sixtappredict16x16_neon.asm
  vp8/common/arm/neon/sixtappredict4x4_neon.asm
  vp8/common/arm/neon/sixtappredict8x4_neon.asm
  vp8/common/arm/neon/sixtappredict8x8_neon.asm
  vp8/common/x86/dequantize_mmx.asm
  vp8/common/x86/idctllm_mmx.asm
  vp8/common/x86/idctllm_sse2.asm
  vp8/common/x86/iwalsh_mmx.asm
  vp8/common/x86/iwalsh_sse2.asm
  vp8/common/x86/loopfilter_block_sse2.asm
  vp8/common/x86/loopfilter_mmx.asm
  vp8/common/x86/loopfilter_sse2.asm
  vp8/common/x86/postproc_mmx.asm
  vp8/common/x86/postproc_sse2.asm
  vp8/common/x86/recon_mmx.asm
  vp8/common/x86/recon_sse2.asm
  vp8/common/x86/subpixel_mmx.asm
  vp8/common/x86/subpixel_sse2.asm
  vp8/common/x86/subpixel_ssse3.asm
  vp8/encoder/arm/armv5te/boolhuff_armv5te.asm
  vp8/encoder/arm/armv5te/vp8_packtokens_armv5.asm
  vp8/encoder/arm/armv5te/vp8_packtokens_mbrow_armv5.asm
  vp8/encoder/arm/armv5te/vp8_packtokens_partitions_armv5.asm
  vp8/encoder/arm/armv6/vp8_fast_quantize_b_armv6.asm
  vp8/encoder/arm/armv6/vp8_mse16x16_armv6.asm
  vp8/encoder/arm/armv6/vp8_sad16x16_armv6.asm
  vp8/encoder/arm/armv6/vp8_short_fdct4x4_armv6.asm
  vp8/encoder/arm/armv6/vp8_subtract_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance16x16_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance8x8_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance_halfpixvar16x16_h_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance_halfpixvar16x16_hv_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance_halfpixvar16x16_v_armv6.asm
  vp8/encoder/arm/armv6/walsh_v6.asm
  vp8/encoder/arm/neon/fastquantizeb_neon.asm
  vp8/encoder/arm/neon/sad16_neon.asm
  vp8/encoder/arm/neon/sad8_neon.asm
  vp8/encoder/arm/neon/shortfdct_neon.asm
  vp8/encoder/arm/neon/subtract_neon.asm
  vp8/encoder/arm/neon/variance_neon.asm
  vp8/encoder/arm/neon/vp8_memcpy_neon.asm
  vp8/encoder/arm/neon/vp8_mse16x16_neon.asm
  vp8/encoder/arm/neon/vp8_shortwalsh4x4_neon.asm
  vp8/encoder/arm/neon/vp8_subpixelvariance16x16_neon.asm
  vp8/encoder/arm/neon/vp8_subpixelvariance16x16s_neon.asm
  vp8/encoder/arm/neon/vp8_subpixelvariance8x8_neon.asm
  vp8/encoder/x86/dct_mmx.asm
  vp8/encoder/x86/dct_sse2.asm
  vp8/encoder/x86/encodeopt.asm
  vp8/encoder/x86/fwalsh_sse2.asm
  vp8/encoder/x86/quantize_mmx.asm
  vp8/encoder/x86/quantize_sse2.asm
  vp8/encoder/x86/quantize_ssse3.asm
  vp8/encoder/x86/quantize_sse4.asm
  vp8/encoder/x86/sad_mmx.asm
  vp8/encoder/x86/sad_sse2.asm
  vp8/encoder/x86/sad_sse3.asm
  vp8/encoder/x86/sad_ssse3.asm
  vp8/encoder/x86/sad_sse4.asm
  vp8/encoder/x86/subtract_mmx.asm
  vp8/encoder/x86/subtract_sse2.asm
  vp8/encoder/x86/temporal_filter_apply_sse2.asm
  vp8/encoder/x86/variance_impl_mmx.asm
  vp8/encoder/x86/variance_impl_sse2.asm
  vp8/encoder/x86/variance_impl_ssse3.asm
  vpx_ports/emms.asm
  vpx_ports/x86_abi_support.asm
  vpx_scale/arm/neon/vp8_vpxyv12_copy_y_neon.asm
  vpx_scale/arm/neon/vp8_vpxyv12_copyframe_func_neon.asm
  vpx_scale/arm/neon/vp8_vpxyv12_extendframeborders_neon.asm
  build/make/ads2gas.pl
  build/make/obj_int_extract.c
  LICENSE
  PATENTS
)

# configure files specific to x86-win32-vs8
cp $1/objdir/x86-win32-vs8/vpx_config.c vpx_config_x86-win32-vs8.c
cp $1/objdir/x86-win32-vs8/vpx_config.asm vpx_config_x86-win32-vs8.asm
cp $1/objdir/x86-win32-vs8/vpx_config.h vpx_config_x86-win32-vs8.h

# Should be same for all platforms...
cp $1/objdir/x86-win32-vs8/vpx_version.h vpx_version.h

# Config files for x86-linux-gcc and other x86 elf platforms
cp $1/objdir/x86-linux-gcc/vpx_config.c vpx_config_x86-linux-gcc.c
cp $1/objdir/x86-linux-gcc/vpx_config.asm vpx_config_x86-linux-gcc.asm
cp $1/objdir/x86-linux-gcc/vpx_config.h vpx_config_x86-linux-gcc.h

# Config files for x86_64-linux-gcc and other x86_64 elf platforms
cp $1/objdir/x86_64-linux-gcc/vpx_config.c vpx_config_x86_64-linux-gcc.c
cp $1/objdir/x86_64-linux-gcc/vpx_config.asm vpx_config_x86_64-linux-gcc.asm
cp $1/objdir/x86_64-linux-gcc/vpx_config.h vpx_config_x86_64-linux-gcc.h

# Copy config files for mac...
cp $1/objdir/x86-darwin9-gcc/vpx_config.c vpx_config_x86-darwin9-gcc.c
cp $1/objdir/x86-darwin9-gcc/vpx_config.asm vpx_config_x86-darwin9-gcc.asm
cp $1/objdir/x86-darwin9-gcc/vpx_config.h vpx_config_x86-darwin9-gcc.h

# Copy config files for Mac64
cp $1/objdir/x86_64-darwin9-gcc/vpx_config.c vpx_config_x86_64-darwin9-gcc.c
cp $1/objdir/x86_64-darwin9-gcc/vpx_config.asm vpx_config_x86_64-darwin9-gcc.asm
cp $1/objdir/x86_64-darwin9-gcc/vpx_config.h vpx_config_x86_64-darwin9-gcc.h

# Config files for arm-linux-gcc
cp $1/objdir/armv7-linux-gcc/vpx_config.c vpx_config_arm-linux-gcc.c
cp $1/objdir/armv7-linux-gcc/vpx_config.h vpx_config_arm-linux-gcc.h

# Config files for generic-gnu
cp $1/objdir/generic-gnu/vpx_config.c vpx_config_generic-gnu.c
cp $1/objdir/generic-gnu/vpx_config.h vpx_config_generic-gnu.h

# Copy common source files into mozilla tree.
for f in ${commonFiles[@]}
do
  mkdir -p -v `dirname $f`
  cp -v $1/$f $f
done

# This has to be renamed because there's already a scalesystemdependent.c in
# vpx_scale/generic/
cp -v $1/vpx_scale/arm/scalesystemdependent.c \
         vpx_scale/arm/arm_scalesystemdependent.c

# Upstream patch to fix variance overflow.
patch -p3 < I1bad27ea.patch

# Upstream patch to remove __inline for compiler compatibility.
patch -p3 < I6f2b218d.patch

# Patch to move SAD and variance functions to common (based on an upstream
# patch).
patch -p3 < I256a37c6.patch

# These get moved by I256a37c6.patch above, but patch won't do the actual move
# for us.
encoderMovedFiles=(
  vp8/encoder/sad_c.c
  vp8/encoder/variance_c.c
  vp8/encoder/arm/variance_arm.c
  vp8/encoder/x86/variance_mmx.c
  vp8/encoder/x86/variance_sse2.c
  vp8/encoder/x86/variance_ssse3.c
  vp8/encoder/variance.h
  vp8/encoder/arm/variance_arm.h
  vp8/encoder/x86/variance_x86.h
  vp8/encoder/arm/armv6/vp8_mse16x16_armv6.asm
  vp8/encoder/arm/armv6/vp8_sad16x16_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance16x16_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance8x8_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance_halfpixvar16x16_h_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance_halfpixvar16x16_hv_armv6.asm
  vp8/encoder/arm/armv6/vp8_variance_halfpixvar16x16_v_armv6.asm
  vp8/encoder/arm/neon/sad16_neon.asm
  vp8/encoder/arm/neon/sad8_neon.asm
  vp8/encoder/arm/neon/variance_neon.asm
  vp8/encoder/arm/neon/vp8_mse16x16_neon.asm
  vp8/encoder/arm/neon/vp8_subpixelvariance16x16_neon.asm
  vp8/encoder/arm/neon/vp8_subpixelvariance16x16s_neon.asm
  vp8/encoder/arm/neon/vp8_subpixelvariance8x8_neon.asm
  vp8/encoder/x86/sad_mmx.asm
  vp8/encoder/x86/sad_sse2.asm
  vp8/encoder/x86/sad_sse3.asm
  vp8/encoder/x86/sad_ssse3.asm
  vp8/encoder/x86/sad_sse4.asm
  vp8/encoder/x86/variance_impl_mmx.asm
  vp8/encoder/x86/variance_impl_sse2.asm
  vp8/encoder/x86/variance_impl_ssse3.asm
)

# Move encoder source files into the common tree.
for f in ${encoderMovedFiles[@]}
do
  mv -v $f ${f/encoder/common}
done

# Patch to fix text relocations in the variance functions.
patch -p3 < textrels.patch

# Patch to use VARIANCE_INVOKE in multiframe_quality_enhance_block().
patch -p3 < variance-invoke.patch

# Upstream patch to fix potential use of uninitialized rate_y.
patch -p3 < I8a35831e.patch

# Upstream patch to reset segmentation map on keyframes.
patch -p3 < I9713c9f0.patch

# Upstream patch to support Android x86 NDK build.
patch -p3 < I42ab00e3.patch

# Upstream patch to align internal mfqe framebuffer dimensions.
patch -p3 < I3915d597.patch

# Patch to compile with Sun Studio on Solaris
patch -p3 < solaris.patch

# Patch to fix errors including C headers in C++
patch -p3 < compile_errors.patch

# Patch to permit vpx users to specify their own <stdint.h> types.
patch -p3 < stdint.patch
