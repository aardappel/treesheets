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

void AddSound(NativeRegistry &natreg) {
    STARTDECL(play_wav) (VM &vm, Value &ins, Value &vol) {
        bool ok = SDLPlaySound(ins.sval()->str(), false, vol.True() ? vol.intval() : 128);
        ins.DECRT(vm);
        return Value(ok);
    }
    ENDDECL2(play_wav, "filename,volume", "SI?", "I",
        "plays a sound defined by a wav file (RAW or MS-ADPCM, any bitrate other than 22050hz 16bit"
        " will automatically be converted on first load). volume in range 1..128, or omit for max."
        " returns false on error");

    STARTDECL(play_sfxr) (VM &vm, Value &ins, Value &vol) {
        bool ok = SDLPlaySound(ins.sval()->str(), true, vol.True() ? vol.intval() : 128);
        ins.DECRT(vm);
        return Value(ok);
    }
    ENDDECL2(play_sfxr, "filename,volume", "SI?", "I",
        "plays a synth sound defined by a .sfs file (use http://www.drpetter.se/project_sfxr.html"
        " to generate these). volume in range 1..128, or omit for max. returns false on error");
}
