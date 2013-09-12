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

[Constructor, PrefControlled]
interface mozAudioContext {

    readonly attribute AudioDestinationNode destination;
    readonly attribute float sampleRate;
    readonly attribute AudioListener listener;

    [Creator, Throws]
    AudioBuffer createBuffer(unsigned long numberOfChannels, unsigned long length, float sampleRate);

    // [Creator, Throws]
    // AudioBuffer createBuffer(ArrayBuffer buffer, boolean mixToMono);

    // AudioNode creation 
    [Creator]
    AudioBufferSourceNode createBufferSource();

    [Creator]
    GainNode createGain();
    [Creator, Throws]
    DelayNode createDelay(optional double maxDelayTime = 1);
    [Creator]
    BiquadFilterNode createBiquadFilter();
    [Creator]
    PannerNode createPanner();

    [Creator]
    DynamicsCompressorNode createDynamicsCompressor();

};

typedef mozAudioContext AudioContext;

