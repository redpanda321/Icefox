/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

class gfxASurface;
class nsIntRegion;
class nsIntSize;

namespace mozilla {

namespace Framebuffer {

//
// The general usage of Framebuffer is
//
// -- in initialization code --
//  Open();
//
// -- ready to paint next frame --
//  nsRefPtr<gfxASurface> backBuffer = BackBuffer();
//  // ...
//  Paint(backBuffer);
//  // ...
//  Present();
//

// Return true if the fbdev was successfully opened.  If this fails, 
// the result of all further calls is undefined.  Open() is idempotent.
bool Open();

// After Close(), the result of all further calls is undefined.
// Close() is idempotent, and Open() can be called again after
// Close().
void Close();

// Return true if the fbdev was successfully opened or the size was
// already cached.
bool GetSize(nsIntSize *aScreenSize);

// Return the buffer to be drawn into, that will be the next frame.
gfxASurface* BackBuffer();

// Swap the front buffer for the back buffer.  |aUpdated| is the
// region of the back buffer that was repainted.
void Present(const nsIntRegion& aUpdated);

} // namespace Framebuffer

} // namespace mozilla
