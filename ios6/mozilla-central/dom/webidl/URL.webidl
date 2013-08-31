/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origins of this IDL file are
 * http://dev.w3.org/2006/webapi/FileAPI/#creating-revoking
 * http://dev.w3.org/2011/webrtc/editor/getusermedia.html#url
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

interface MediaStream;
 
interface URL {
  [Throws]
  static DOMString? createObjectURL(Blob blob, optional objectURLOptions options);
  [Throws]
  static DOMString? createObjectURL(MediaStream stream, optional objectURLOptions options);
  static void revokeObjectURL(DOMString url);
};

dictionary objectURLOptions
{
/* boolean autoRevoke = true; */ /* not supported yet */
};
