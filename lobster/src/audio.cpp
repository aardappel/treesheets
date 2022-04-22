// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#include "lobster/sdlinterface.h"

using namespace lobster;

void AddSound(NativeRegistry &nfr) {

nfr("play_wav", "filename,loops,prio", "SI?I?", "I",
    "plays a sound defined by a wav file (RAW or MS-ADPCM, any bitrate other than 22050hz 16bit"
    " will automatically be converted on first load). the default volume is the max volume (1.0)"
    " loops is the number of repeats to play (-1 repeats endlessly, omit for no repeats)."
    " prio is the priority of the sound which determines whether it can be deleted or not"
    " in case of too many play function calls (defaults to 0)"
    " returns the assigned channel number (1..8) or 0 on error",
    [](StackPtr &, VM &, Value &ins, Value &loops, Value &prio) {
        int ch = SDLPlaySound(ins.sval()->strv(), SOUND_WAV, 1.0, loops.intval(), prio.intval());
        return Value(ch);
    });

nfr("load_wav", "filename", "S", "B",
    "loads a sound the same way play_sound does, but ahead of playback, to avoid any"
    " delays later. returns false on error",
    [](StackPtr &, VM &, Value &ins) {
        int ok = SDLLoadSound(ins.sval()->strv(), SOUND_WAV);
        return Value(ok);
    });

nfr("play_sfxr", "filename,loops,prio", "SI?I?", "I",
    "plays a synth sound defined by a .sfs file (use http://www.drpetter.se/project_sfxr.html"
    " to generate these). the default volume is the max volume (1.0)"
    " loops is the number of repeats to play (-1 repeats endlessly, omit for no repeats)."
    " prio is the priority of the sound which determines whether it can be deleted or not"
    " in case of too many play function calls (defaults to 0)"
    " returns the assigned channel number (1..8) or 0 on error",
    [](StackPtr &, VM &, Value &ins, Value &loops, Value &prio) {
        int ch = SDLPlaySound(ins.sval()->strv(), SOUND_SFXR, 1.0, loops.intval(), prio.intval());
        return Value(ch);
    });

nfr("load_sfxr", "filename", "S", "B",
    "loads a sound the same way play_sfxr does, but ahead of playback, to avoid any"
    " delays later. returns false on error",
    [](StackPtr &, VM &, Value &ins) {
        int ok = SDLLoadSound(ins.sval()->strv(), SOUND_SFXR);
        return Value(ok);
    });

nfr("play_ogg", "filename,loops,prio", "SI?I?", "I",
    "plays an ogg file. the default volume is the max volume (1.0)"
    " loops is the number of repeats to play (-1 repeats endlessly, omit for no repeats)."
    " prio is the priority of the sound which determines whether it can be deleted or not"
    " in case of too many play function calls (defaults to 0)"
    " returns the assigned channel number (1..8) or 0 on error",
    [](StackPtr &, VM &, Value &ins, Value &loops, Value &prio) {
        int ch = SDLPlaySound(ins.sval()->strv(), SOUND_OGG, 1.0, loops.intval(), prio.intval());
        return Value(ch);
    });

nfr("load_ogg", "filename", "S", "B",
    "loads a sound the same way play_ogg does, but ahead of playback, to avoid any"
    " delays later. returns false on error",
    [](StackPtr &, VM &, Value &ins) {
        int ok = SDLLoadSound(ins.sval()->strv(), SOUND_OGG);
        return Value(ok);
    });

nfr("sound_status", "channel", "I", "I",
    "provides the status of the specified sound channel."
    " returns -1 on error or if the channel does not exist, 0 if the channel is free,"
    " 1 if it is playing, and 2 if the channel is active but paused.",
    [](StackPtr &, VM &, Value &ch) {
        int ch_idx = ch.intval();
        if (ch_idx > 0) // we disallow 0 (which would then be -1; all channels in SDL_Mixer) because it is our error value!
            return Value(SDLSoundStatus(ch_idx));
        else
            return Value(-1);
    });

nfr("sound_halt", "channel", "I", "",
    "terminates a specific sound channel.",
    [](StackPtr &, VM &, Value &ch) {
        int ch_idx = ch.intval();
        if (ch_idx > 0)
            SDLHaltSound(ch_idx);
        return NilVal();
    });

nfr("sound_pause", "channel", "I", "",
    "pauses the specified sound channel.",
    [](StackPtr &, VM &, Value &ch) {
        int ch_idx = ch.intval();
        if (ch_idx > 0)
            SDLPauseSound(ch_idx);
        return NilVal();
    });

nfr("sound_resume", "channel", "I", "",
    "resumes a sound that was paused.",
    [](StackPtr &, VM &, Value &ch) {
        int ch_idx = ch.intval();
        if (ch_idx > 0)
            SDLResumeSound(ch_idx);
        return NilVal();
    });

nfr("sound_volume", "channel,volume", "IF", "",
    "sets the channel volume in the range 0..1.",
    [](StackPtr &, VM &, Value &ch, Value &vol) {
        int ch_idx = ch.intval();
        if (ch_idx > 0)
            SDLSetVolume(ch_idx, vol.fltval());
        return NilVal();
    });

}
