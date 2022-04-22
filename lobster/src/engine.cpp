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

// Engine integration with Lobster VM.

#include "lobster/stdafx.h"

#include "lobster/compiler.h"  // For RegisterBuiltin().

#include "lobster/sdlinterface.h"

#include "lobster/compiler.h"


// The code below allows for lightweight "plugins" to the
// engine, by seeing if there's a header file with this name
// in the "projects" directory of the Lobster repo.
// This header can define custom functions needed for your
// project, or include files that do, allowing you to extend
// Lobster without the needs for DLL/so files or whatever.
// "projects" is already in .gitignore so can even be a
// separate git repo.
#if __has_include("../../projects/include/lobster_engine_plugins.h")
    #include "../../projects/include/lobster_engine_plugins.h"
    #define HAVE_PLUGINS
    void AddPlugins(NativeRegistry &nfr);
#endif


using namespace lobster;

extern void AddGraphics(NativeRegistry &nfr);
extern void AddFont(NativeRegistry &nfr);
extern void AddSound(NativeRegistry &nfr);
extern void AddPhysics(NativeRegistry &nfr);
extern void AddNoise(NativeRegistry &nfr);
extern void AddMeshGen(NativeRegistry &nfr);
extern void AddCubeGen(NativeRegistry &nfr);
extern void AddVR(NativeRegistry &nfr);
extern void AddSteam(NativeRegistry &nfr);
extern void AddIMGUI(NativeRegistry &nfr);

namespace lobster {

FileLoader EnginePreInit(NativeRegistry &nfr) {
    RegisterBuiltin(nfr, "graphics",  AddGraphics);
    RegisterBuiltin(nfr, "font",      AddFont);
    RegisterBuiltin(nfr, "sound",     AddSound);
    RegisterBuiltin(nfr, "physics",   AddPhysics);
    RegisterBuiltin(nfr, "noise",     AddNoise);
    RegisterBuiltin(nfr, "meshgen",   AddMeshGen);
    RegisterBuiltin(nfr, "cubegen",   AddCubeGen);
    RegisterBuiltin(nfr, "vr",        AddVR);
    RegisterBuiltin(nfr, "steam",     AddSteam);
    RegisterBuiltin(nfr, "imgui",     AddIMGUI);
    #ifdef HAVE_PLUGINS
        RegisterBuiltin(nfr, "plugin", AddPlugins);
    #endif
    return SDLLoadFile;
}

}  // namespace lobster
