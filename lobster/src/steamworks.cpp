// Copyright 2017 Wouter van Oortmerssen. All rights reserved.
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

#include "lobster/glinterface.h"
#include "lobster/sdlinterface.h"

#ifdef PLATFORM_STEAMWORKS

#include "steam/steam_api.h"

struct SteamState {
    bool steamoverlayactive = false;
    STEAM_CALLBACK(SteamState, OnGameOverlayActivated, GameOverlayActivated_t);
    STEAM_CALLBACK(SteamState, OnScreenshotRequested, ScreenshotRequested_t);
};

void SteamState::OnGameOverlayActivated(GameOverlayActivated_t *callback) {
    steamoverlayactive = callback->m_bActive;
    Output(OUTPUT_INFO, "steam overlay toggle: ", steamoverlayactive);
}

void SteamState::OnScreenshotRequested(ScreenshotRequested_t *) {
    Output(OUTPUT_INFO, "steam screenshot requested");
    auto size = GetScreenSize();
    auto pixels = ReadPixels(int2(0), size);
    SteamScreenshots()->WriteScreenshot(pixels, size.x * size.y * 3, size.x, size.y);
    delete[] pixels;
}

extern "C" void __cdecl SteamAPIDebugTextHook(int severity, const char *debugtext) {
    Output(severity ? OUTPUT_WARN : OUTPUT_INFO, debugtext);
}

SteamState *steam = nullptr;

#endif  // PLATFORM_STEAMWORKS

void SteamShutDown() {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            delete steam;
            SteamAPI_Shutdown();
        }
        steam = nullptr;
    #endif  // PLATFORM_STEAMWORKS
}

int SteamInit(uint appid, bool screenshots) {
    SteamShutDown();
    #ifdef PLATFORM_STEAMWORKS
        #ifndef NDEBUG
            (void)appid;
        #else
            if (appid && SteamAPI_RestartAppIfNecessary(appid)) {
                Output(OUTPUT_INFO, "Not started from Steam");
                return -1;
            }
        #endif
        bool steaminit = SteamAPI_Init();
        Output(OUTPUT_INFO, "Steam init: ", steaminit);
        if (!steaminit) return 0;
        SteamUtils()->SetWarningMessageHook(&SteamAPIDebugTextHook);
        steam = new SteamState();
        SteamUserStats()->RequestCurrentStats();
        if (screenshots) SteamScreenshots()->HookScreenshots(true);
        return 1;
    #else
        return 0;
    #endif  // PLATFORM_STEAMWORKS
}

void SteamUpdate() {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) SteamAPI_RunCallbacks();
    #endif  // PLATFORM_STEAMWORKS
}

const char *UserName() {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) return SteamFriends()->GetPersonaName();
    #endif  // PLATFORM_STEAMWORKS
    return "";
}

bool UnlockAchievement(const char *name) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            auto ok = SteamUserStats()->SetAchievement(name);
            return SteamUserStats()->StoreStats() && ok;  // Force this to run.
        }
    #else
        (void)name;
    #endif  // PLATFORM_STEAMWORKS
    return false;
}

int SteamReadFile(const char *fn, string &buf) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            auto len = SteamRemoteStorage()->GetFileSize(fn);
            if (len) {
                buf.resize(len);
                return SteamRemoteStorage()->FileRead(fn, (void *)buf.data(), len);
            }
        }
    #endif  // PLATFORM_STEAMWORKS
    return 0;
}

bool SteamWriteFile(const char *fn, const void *buf, size_t len) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            return SteamRemoteStorage()->FileWrite(fn, buf, (int)len);
        }
    #endif  // PLATFORM_STEAMWORKS
    return false;
}

bool OverlayActive() {
    #ifdef PLATFORM_STEAMWORKS
        return steam && steam->steamoverlayactive;
    #endif  // PLATFORM_STEAMWORKS
    return false;
}

using namespace lobster;

void AddSteam(NativeRegistry &natreg) {
    STARTDECL(steam_init) (VM &, Value &appid, Value &ss) {
        return Value(SteamInit((uint)appid.ival(), ss.True()));
    }
    ENDDECL2(steam_init, "appid,allowscreenshots", "II", "I",
        "initializes SteamWorks. returns 1 if succesful, 0 on failure. Specify a non-0 appid if you"
        " want to restart from steam if this wasn't started from steam (the return value in this"
        " case will be -1 to indicate you should terminate this instance). If you don't specify an"
        " appid here or in steam_appid.txt, init will likely fail. The other functions can still be"
        " called even if steam isn't active."
        " allowscreenshots automatically uploads screenshots to steam (triggered by steam).");

    STARTDECL(steam_overlay) (VM &) {
        return Value(OverlayActive());
    }
    ENDDECL0(steam_overlay, "", "", "I",
        "returns true if the steam overlay is currently on (you may want to auto-pause if so)");

    STARTDECL(steam_username) (VM &vm) {
        return Value(vm.NewString(UserName()));
    }
    ENDDECL0(steam_username, "", "", "S",
        "returns the name of the steam user, or empty string if not available.");

    STARTDECL(steam_unlock_achievement) (VM &vm, Value &name) {
        auto ok = UnlockAchievement(name.sval()->str());
        name.DECRT(vm);
        return Value(ok);
    }
    ENDDECL1(steam_unlock_achievement, "achievementname", "S", "I",
        "Unlocks an achievement and shows the achievement overlay if not already achieved before."
        " Will also Q-up saving achievement to Steam."
        " Returns true if succesful.");

    STARTDECL(steam_write_file) (VM &vm, Value &file, Value &contents) {
        auto fn = file.sval()->str();
        auto s = contents.sval();
        auto ok = SteamWriteFile(fn, s->str(), s->len);
        if (!ok) {
            ok = WriteFile(fn, true, s->strv());
        }
        file.DECRT(vm);
        contents.DECRT(vm);
        return Value(ok);
    }
    ENDDECL2(steam_write_file, "file,contents", "SS", "I",
        "writes a file with the contents of a string to the steam cloud, or local storage if that"
        " fails, returns false if writing wasn't possible at all");

    STARTDECL(steam_read_file) (VM &vm, Value &file) {
        auto fn = file.sval()->str();
        string buf;
        auto len = SteamReadFile(fn, buf);
        if (!len) len = (int)LoadFile(fn, &buf);
        file.DECRT(vm);
        if (len < 0) return Value();
        auto s = vm.NewString(buf);
        return Value(s);
    }
    ENDDECL1(steam_read_file, "file", "S", "S?",
        "returns the contents of a file as a string from the steam cloud if available, or otherwise"
        " from local storage, or nil if the file can't be found at all.");
}

