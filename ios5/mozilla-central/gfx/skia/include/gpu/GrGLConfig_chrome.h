
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef GrGLConfig_chrome_DEFINED
#define GrGLConfig_chrome_DEFINED

// glGetError() forces a sync with gpu process on chrome
#define GR_GL_CHECK_ERROR_START         0

// ANGLE creates a temp VB for vertex attributes not specified per-vertex.
#define GR_GL_NO_CONSTANT_ATTRIBUTES    GR_WIN32_BUILD

// For RGBA teximage/readpixels ANGLE will sw-convert to/from BGRA.
#define GR_GL_RGBA_8888_PIXEL_OPS_SLOW  GR_WIN32_BUILD

// ANGLE can go faster if the entire fbo is read rather than a subrect
#define GR_GL_FULL_READPIXELS_FASTER_THAN_PARTIAL GR_WIN32_BUILD

// cmd buffer allocates memory and memsets it to zero when it sees glBufferData
// with NULL.
#define GR_GL_USE_BUFFER_DATA_NULL_HINT 0

// chrome uses this to set the context on each GL call.
#define GR_GL_PER_GL_FUNC_CALLBACK      1

#endif
