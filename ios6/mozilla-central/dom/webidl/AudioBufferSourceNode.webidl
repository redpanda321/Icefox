/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://dvcs.w3.org/hg/audio/raw-file/tip/webaudio/specification.html
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

[PrefControlled]
interface AudioBufferSourceNode : AudioSourceNode {

    const unsigned short UNSCHEDULED_STATE = 0;
    const unsigned short SCHEDULED_STATE = 1;
    const unsigned short PLAYING_STATE = 2;
    const unsigned short FINISHED_STATE = 3;

    //readonly attribute unsigned short playbackState;

    // Playback this in-memory audio asset  
    // Many sources can share the same buffer  
    attribute AudioBuffer? buffer;

    //attribute AudioParam playbackRate;
    //attribute boolean loop;

    void start(double when);
    //void start(double when, double grainOffset, double grainDuration);
    void stop(double when);

};

