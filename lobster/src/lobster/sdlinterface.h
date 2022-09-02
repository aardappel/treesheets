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

// simple interface for SDL (that doesn't depend on its headers)

// See also modules/gl.lobster
enum InitFlags { INIT_FULLSCREEN = 1, INIT_NO_VSYNC = 2, INIT_LINEAR_COLOR = 4 };

extern string SDLInit(string_view title, const int2 &screensize, InitFlags flags, int samples);
extern void SDLRequireGLVersion(int major, int minor);
extern bool SDLFrame();
extern void SDLShutdown();
extern void SDLTitle(string_view title);
extern bool SDLIsMinimized();
extern void SDLWindowMinMax(int dir);

extern const int2 &GetScreenSize();

extern const int2 &GetFinger(int i, bool delta);
extern pair<int64_t, int64_t> GetKS(string_view name);
extern bool KeyRepeat(string_view name);
extern double GetKeyTime(string_view name, int on);
extern int2 GetKeyPos(string_view name, int on);
extern float GetJoyAxis(int i);
extern string &GetDroppedFile();

extern double SDLTime();
extern double SDLDeltaTime();
extern vector<float> &SDLGetFrameTimeLog();

extern int SDLWheelDelta();

extern bool SDLCursor(bool on);
extern bool SDLGrab(bool on);

extern void SDLMessageBox(string_view title, string_view msg);

enum SoundType { SOUND_WAV, SOUND_SFXR, SOUND_OGG };
extern int SDLLoadSound(string_view filename, SoundType st);
extern int SDLPlaySound(string_view filename, SoundType st, float vol, int loops, int pri);
extern void SDLHaltSound(int ch);
extern void SDLPauseSound(int ch);
extern void SDLResumeSound(int ch);
extern void SDLSetVolume(int ch, float vol);
extern void SDLSetPosition(int ch, float3 vecfromlistener, float3 listenerfwd, float attnscale);
extern int SDLSoundStatus(int ch);
extern void SDLSoundClose();

extern int64_t SDLLoadFile(string_view absfilename, string *dest, int64_t start, int64_t len);

extern bool ScreenShot(string_view filename);

extern void SDLTestMode();

extern int SDLScreenDPI(int screen);

struct TextInput {
    string text;
    string editing;
    int cursor = 0;
    int len = 0;
};

extern void SDLStartTextInput(int2 pos, int2 size);
extern TextInput &SDLTextInputState();
extern void SDLTextInputSet(string_view t);
extern void SDLEndTextInput();

extern bool GraphicsFrameStart();
extern void GraphicsShutDown();

extern string SDLDebuggerWindow();
extern bool SDLDebuggerFrame();
extern void SDLDebuggerOff();