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
    LOG_INFO("steam overlay toggle: ", steamoverlayactive);
}

void SteamState::OnScreenshotRequested(ScreenshotRequested_t *) {
    LOG_INFO("steam screenshot requested");
    auto size = GetScreenSize();
    auto pixels = ReadPixels(int2(0), size);
    SteamScreenshots()->WriteScreenshot(pixels, size.x * size.y * 3, size.x, size.y);
    delete[] pixels;
}

extern "C" void __cdecl SteamAPIDebugTextHook(int severity, const char *debugtext) {
    if (severity) LOG_WARN(debugtext) else LOG_INFO(debugtext);
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

int SteamInit(iint appid, bool screenshots) {
    SteamShutDown();
    #ifdef PLATFORM_STEAMWORKS
        #ifndef NDEBUG
            (void)appid;
        #else
            if (appid && SteamAPI_RestartAppIfNecessary((uint32_t)appid)) {
                LOG_INFO("Not started from Steam");
                return -1;
            }
        #endif
        bool steaminit = SteamAPI_Init();
        LOG_INFO("Steam init: ", steaminit);
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

bool UnlockAchievement(string_view name) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            auto ok = SteamUserStats()->SetAchievement(null_terminated(name));
            return SteamUserStats()->StoreStats() && ok;  // Force this to run.
        }
    #else
        (void)name;
    #endif  // PLATFORM_STEAMWORKS
    return false;
}

int SteamReadFile(string_view fn, string &buf) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            auto len = SteamRemoteStorage()->GetFileSize(null_terminated(fn));
            if (len) {
                buf.resize(len);
                return SteamRemoteStorage()->FileRead(null_terminated(fn), (void *)buf.data(), len);
            }
        }
    #endif  // PLATFORM_STEAMWORKS
    return 0;
}

bool SteamWriteFile(string_view fn, string_view buf) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            return SteamRemoteStorage()->FileWrite(null_terminated(fn), buf.data(), (int)buf.size());
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

void AddSteam(NativeRegistry &nfr) {

nfr("steam_init", "appid,allowscreenshots", "IB", "I",
    "initializes SteamWorks. returns 1 if succesful, 0 on failure. Specify a non-0 appid if you"
    " want to restart from steam if this wasn't started from steam (the return value in this"
    " case will be -1 to indicate you should terminate this instance). If you don't specify an"
    " appid here or in steam_appid.txt, init will likely fail. The other functions can still be"
    " called even if steam isn't active."
    " allowscreenshots automatically uploads screenshots to steam (triggered by steam).",
    [](StackPtr &, VM &, Value &appid, Value &ss) {
        return Value(SteamInit(appid.ival(), ss.True()));
    });

nfr("steam_overlay", "", "", "B",
    "returns true if the steam overlay is currently on (you may want to auto-pause if so)",
    [](StackPtr &, VM &) {
        return Value(OverlayActive());
    });

nfr("steam_username", "", "", "S",
    "returns the name of the steam user, or empty string if not available.",
    [](StackPtr &, VM &vm) {
        return Value(vm.NewString(UserName()));
    });

nfr("steam_unlock_achievement", "achievementname", "S", "B",
    "Unlocks an achievement and shows the achievement overlay if not already achieved before."
    " Will also Q-up saving achievement to Steam."
    " Returns true if succesful.",
    [](StackPtr &, VM &, Value &name) {
        auto ok = UnlockAchievement(name.sval()->strv());
        return Value(ok);
    });

nfr("steam_write_file", "file,contents", "SS", "B",
    "writes a file with the contents of a string to the steam cloud, or local storage if that"
    " fails, returns false if writing wasn't possible at all",
    [](StackPtr &, VM &, Value &file, Value &contents) {
        auto fn = file.sval()->strv();
        auto s = contents.sval();
        auto ok = SteamWriteFile(fn, s->strv());
        if (!ok) {
            ok = WriteFile(fn, true, s->strv());
        }
        return Value(ok);
    });

nfr("steam_read_file", "file", "S", "S?",
    "returns the contents of a file as a string from the steam cloud if available, or otherwise"
    " from local storage, or nil if the file can't be found at all.",
    [](StackPtr &, VM &vm, Value &file) {
        auto fn = file.sval()->strv();
        string buf;
        auto len = SteamReadFile(fn, buf);
        if (!len) len = (int)LoadFile(fn, &buf);
        if (len < 0) return NilVal();
        auto s = vm.NewString(buf);
        return Value(s);
    });

}  // AddSteam

