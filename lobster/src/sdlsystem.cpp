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

#include "lobster/vmdata.h"
#include "lobster/sdlincludes.h"
#include "lobster/sdlinterface.h"
#include "lobster/glinterface.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable: 4244)
#endif
#include "stb/stb_image_write.h"
#ifdef _MSC_VER
  #pragma warning(pop)
#endif

SDL_Window *_sdl_window = nullptr;
SDL_GLContext _sdl_context = nullptr;

SDL_Window *_sdl_debugger_window = nullptr;
SDL_GLContext _sdl_debugger_context = nullptr;


/*
// FIXME: document this, especially the ones containing spaces.

mouse1 mouse2 mouse3...
backspace tab clear return pause escape space delete
! " # $ & ' ( ) * + , - . / 0 1 2 3 4 5 6 7 8 9 : ; < = > ? @ [ \ ] ^ _ `
a b c d e f g h i j k l m n o p q r s t u v w x y z
[0] [1] [2] [3] [4] [5] [6] [7] [8] [9] [.] [/] [*] [-] [+]
enter equals up down right left insert home end page up page down
f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12 f13 f14 f15
numlock caps lock scroll lock right shift left shift right ctrl left ctrl right alt left alt
right meta left meta left super right super alt gr compose help print screen sys req break

For controllers (all starting with controller_):

a b x y back guide start leftstick rightstick leftshoulder rightshoulder
dpup dpdown dpleft dpright misc1 paddle1 paddle2 paddle3 paddle4 touchpad

joy0 and up give raw joystick values with no mapping.
*/


struct KeyState {
    int64_t frames_down = 0;
    int64_t frames_up = 2;  // Unpressed at initialization: has been up for more than 1 frame.
    bool repeat = false;

    double lasttime[2] = { -1, -1 };
    int2 lastpos[2] = { int2(-1, -1), int2(-1, -1) };

    void Reset() {
        frames_down = 0;
        frames_up = 2;
        repeat = false;
    }

    void Set(bool on) {
        if (on) {
            frames_down = 1;
            if (frames_up > 1) frames_up = 0;  // Not in this frame, so turn off.
        } else {
            frames_up = 1;
            if (frames_down > 1) frames_down = 0;  // Not in this frame, so turn off.
        }
    }

    void FrameAdvance() {
        if (frames_up) {
            frames_up++;
            frames_down = 0;  // In case it was down+up last frame.
        }
        if (frames_down) {
            frames_down++;
            // This situation doesn't occur, a single frame up+down will get interpreted as down+up above.
            // Hopefully that will be much more rare than single frame down+up :)
            assert(!frames_up);
        }
        repeat = false;
    }

    pair<int64_t, int64_t> State() {
        return { frames_down, frames_up };
    }

    pair<int64_t, int64_t> Prev() {
        return { std::max(int64_t(0), frames_down - 1),
                 std::max(int64_t(0), frames_up - 1) };
    }
};

map<string, KeyState, less<>> keymap;

TextInput textinput;

int mousewheeldelta = 0;

int skipmousemotion = 3;

double frametime = 1.0f / 60.0f, lasttime = 0;
uint64_t timefreq = 0, timestart = 0;
int frames = 0;
vector<float> frametimelog;

int2 screensize = int2_0;
int2 inputscale = int2_1;

bool fullscreen = false;
bool cursor = true;
int cursorx = 0, cursory = 0;
bool landscape = true;
bool minimized = false;
bool noninteractivetestmode = false;

const int MAXAXES = 16;
float joyaxes[MAXAXES] = { 0 };
float controller_axes[SDL_CONTROLLER_AXIS_MAX] = { 0 };

struct Finger {
    SDL_FingerID id;
    int2 mousepos;
    int2 mousedelta;
    bool used;

    Finger() : id(0), mousepos(-1), mousedelta(0), used(false) {};
};

const int MAXFINGERS = 10;
Finger fingers[MAXFINGERS];


void updatebutton(string &name, bool on, int posfinger, bool repeat) {
    auto &ks = keymap[name];
    if (!repeat) {
        ks.Set(on);
        ks.lasttime[on] = lasttime;
        ks.lastpos[on] = fingers[posfinger].mousepos;
    }
    ks.repeat = repeat;
}

void updatemousebutton(int button, int finger, bool on) {
    string name = "mouse";
    name += '0' + (char)button;
    if (finger) name += '0' + (char)finger;
    updatebutton(name, on, finger, false);
}

void clearfingers(bool delta) {
    for (auto &f : fingers) (delta ? f.mousedelta : f.mousepos) = int2(0);
}

int findfinger(SDL_FingerID id, bool remove) {
    for (auto &f : fingers) if (f.id == id && f.used) {
        if (remove) {
            // would be more correct to clear mouse position here, but that doesn't work with delayed touch..
            // would have to delay it too
            f.used = false;
        }
        return int(&f - fingers);
    }
    if (remove) return MAXFINGERS - 1; // FIXME: this is masking a bug...
    assert(!remove);
    for (auto &f : fingers) if (!f.used) {
        f.id = id;
        f.used = true;
        return int(&f - fingers);
    }
    assert(0);
    return 0;
}

const int2 &GetFinger(int i, bool delta) {
    auto &f = fingers[max(min(i, MAXFINGERS - 1), 0)];
    return delta ? f.mousedelta : f.mousepos;
}

float GetJoyAxis(int i) {
    return joyaxes[max(min(i, MAXAXES - 1), 0)];
}

float GetControllerAxis(int i) {
    return controller_axes[max(min(i, SDL_CONTROLLER_AXIS_MAX - 1), 0)];
}

int updatedragpos(SDL_TouchFingerEvent &e, Uint32 et) {
    int numfingers = SDL_GetNumTouchFingers(e.touchId);
    //assert(numfingers && e.fingerId < numfingers);
    for (int i = 0; i < numfingers; i++) {
        auto finger = SDL_GetTouchFinger(e.touchId, i);
        if (finger->id == e.fingerId) {
            // this is a bit clumsy as SDL has a list of fingers and so do we, but they work a bit differently
            int j = findfinger(e.fingerId, et == SDL_FINGERUP);
            auto &f = fingers[j];
            auto ep = float2(e.x, e.y);
            auto ed = float2(e.dx, e.dy);
            auto xy = ep * float2(screensize);

            // FIXME: converting back to int coords even though touch theoretically may have higher res
            f.mousepos = int2(xy * float2(inputscale));
            f.mousedelta += int2(ed * float2(screensize));
            return j;
        }
    }
    //assert(0);
    return 0;
}

string dropped_file;
string &GetDroppedFile() { return dropped_file; }

string SDLError(const char *msg) {
    string s = string_view(msg) + ": " + SDL_GetError();
    LOG_WARN(s);
    SDLShutdown();
    return s;
}

int SDLHandleAppEvents(void * /*userdata*/, SDL_Event *event) {
    // NOTE: This function only called on mobile devices it appears.
    switch (event->type) {
        case SDL_APP_TERMINATING:
            /* Terminate the app.
             Shut everything down before returning from this function.
             */
            return 0;
        case SDL_APP_LOWMEMORY:
            /* You will get this when your app is paused and iOS wants more memory.
             Release as much memory as possible.
             */
            return 0;
        case SDL_APP_WILLENTERBACKGROUND:
            LOG_DEBUG("SDL_APP_WILLENTERBACKGROUND");
            minimized = true;
            /* Prepare your app to go into the background.  Stop loops, etc.
             This gets called when the user hits the home button, or gets a call.
             */
            return 0;
        case SDL_APP_DIDENTERBACKGROUND:
            LOG_DEBUG("SDL_APP_DIDENTERBACKGROUND");
            /* This will get called if the user accepted whatever sent your app to the background.
             If the user got a phone call and canceled it,
             you'll instead get an SDL_APP_DIDENTERFOREGROUND event and restart your loops.
             When you get this, you have 5 seconds to save all your state or the app will be terminated.
             Your app is NOT active at this point.
             */
            return 0;
        case SDL_APP_WILLENTERFOREGROUND:
            LOG_DEBUG("SDL_APP_WILLENTERFOREGROUND");
            /* This call happens when your app is coming back to the foreground.
             Restore all your state here.
             */
            return 0;
        case SDL_APP_DIDENTERFOREGROUND:
            LOG_DEBUG("SDL_APP_DIDENTERFOREGROUND");
            /* Restart your loops here.
             Your app is interactive and getting CPU again.
             */
            minimized = false;
            return 0;
        default:
            /* No special processing, add it to the event queue */
            return 1;
    }
}

const int2 &GetScreenSize() { return screensize; }

void ScreenSizeChanged() {
    int2 inputsize;
    SDL_GetWindowSize(_sdl_window, &inputsize.x, &inputsize.y);
    SDL_GL_GetDrawableSize(_sdl_window, &screensize.x, &screensize.y);
    inputscale = screensize / inputsize;
}

SDL_GameController *find_controller() {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            return SDL_GameControllerOpen(i);
        }
    }
    return nullptr;
}
SDL_GameController *controller = nullptr;

#ifdef PLATFORM_ES3
int gl_major = 3, gl_minor = 0;
#else
int gl_major = 3, gl_minor = 2;
string glslversion = "150";
#endif
void SDLRequireGLVersion(int major, int minor) {
    #ifdef PLATFORM_WINNIX
        gl_major = major;
        gl_minor = minor;
        glslversion = cat(major, minor, "0");
    #endif
};

string SDLInit(string_view_nt title, const int2 &desired_screensize, InitFlags flags, int samples) {
    MakeDPIAware();
    TextToSpeechInit();  // Needs to be before SDL_Init because COINITBASE_MULTITHREADED
    // SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO /* | SDL_INIT_AUDIO*/) < 0) {
        return SDLError("Unable to initialize SDL");
    }

    SDL_SetEventFilter(SDLHandleAppEvents, nullptr);

    LOG_INFO("SDL initialized...");

    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_WARN);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_minor);
    #ifdef PLATFORM_ES3
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    #else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        #if defined(__APPLE__) || defined(_WIN32)
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples > 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
        #endif
    #endif

    //SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);      // set this if we're in 2D mode for speed on mobile?
    SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 1);    // because we redraw the screen each frame
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    #ifndef __EMSCRIPTEN__ // FIXME: https://github.com/emscripten-ports/SDL2/issues/86
        if (flags & INIT_LINEAR_COLOR) SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
    #endif

    #ifdef _DEBUG
        // Hopefully get some more validation out of OpenGL.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    #endif

    LOG_INFO("SDL about to figure out display mode...");

    // FIXME: for emscripten, this picks screen size, not browser window size, and doesn't resize.
    #ifdef PLATFORM_ES3
        landscape = desired_screensize.x >= desired_screensize.y;
        int modes = SDL_GetNumDisplayModes(0);
        screensize = int2(320, 200);
        for (int i = 0; i < modes; i++) {
            SDL_DisplayMode mode;
            SDL_GetDisplayMode(0, i, &mode);
            LOG_INFO("mode: ", mode.w, " ", mode.h);
            if (landscape ? mode.w > screensize.x : mode.h > screensize.y) {
                screensize = int2(mode.w, mode.h);
            }
        }
        LOG_INFO("chosen resolution: ", screensize.x, " ", screensize.y);
        LOG_INFO("SDL about to create window...");
        auto wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;
        #ifdef __EMSCRIPTEN__
            wflags |= SDL_WINDOW_RESIZABLE;
        #endif
        _sdl_window = SDL_CreateWindow(title.c_str(),
                                       SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                       screensize.x, screensize.y, wflags);
        LOG_INFO(_sdl_window ? "SDL window passed..." : "SDL window FAILED...");
        if (landscape) SDL_SetHint("SDL_HINT_ORIENTATIONS", "LandscapeLeft LandscapeRight");
    #else
        int display = 0;  // FIXME: we're not dealing with multiple displays.
        float dpi = 0;
        const float default_dpi =
            #ifdef __APPLE__
                72.0f;
            #else
                96.0f;
            #endif
        if (SDL_GetDisplayDPI(display, NULL, &dpi, NULL)) dpi = default_dpi;
        LOG_INFO(cat("dpi: ", dpi));
        screensize = desired_screensize * int(dpi) / int(default_dpi);
        // STARTUP-TIME-COST: 0.16 sec.
        _sdl_window = SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            screensize.x, screensize.y,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
                (flags & INIT_NO_RESIZABLE ? 0 : SDL_WINDOW_RESIZABLE) |
                (flags & INIT_BORDERLESS ? SDL_WINDOW_BORDERLESS : 0) |
                (flags & INIT_MAXIMIZED ? SDL_WINDOW_MAXIMIZED : 0) |
                (flags & INIT_FULLSCREEN ? (flags & INIT_NATIVE ? SDL_WINDOW_FULLSCREEN
                                                                : SDL_WINDOW_FULLSCREEN_DESKTOP)
                                         : 0));
    #endif
    ScreenSizeChanged();
    LOG_INFO("obtained resolution: ", screensize.x, " ", screensize.y);

    if (!_sdl_window)
        return SDLError("Unable to create window");

    LOG_INFO("SDL window opened...");


    _sdl_context = SDL_GL_CreateContext(_sdl_window);
    LOG_INFO(_sdl_context ? "SDL context passed..." : "SDL context FAILED...");
    if (!_sdl_context) return SDLError("Unable to create OpenGL context");

    LOG_INFO("SDL OpenGL context created...");

    #ifndef __IOS__
        if (SDL_GL_SetSwapInterval(flags & INIT_NO_VSYNC ? 0 : -1) < 0) SDL_GL_SetSwapInterval(1);
    #endif

    auto gl_err = OpenGLInit(samples, flags & INIT_LINEAR_COLOR);

    // STARTUP-TIME-COST: 0.08 sec. (due to controller, not joystick)
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) >= 0 &&
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) >= 0) {
        SDL_JoystickEventState(SDL_ENABLE);
        SDL_JoystickUpdate();
        for(int i = 0; i < SDL_NumJoysticks(); i++) {
            SDL_Joystick *joy = SDL_JoystickOpen(i);
            if (joy) {
                LOG_INFO("Detected joystick: ", SDL_JoystickName(joy), " (",
                                    SDL_JoystickNumAxes(joy), " axes, ",
                                    SDL_JoystickNumButtons(joy), " buttons, ",
                                    SDL_JoystickNumBalls(joy), " balls, ",
                                    SDL_JoystickNumHats(joy), " hats)");
            };
        };
        controller = find_controller();
    }

    timestart = SDL_GetPerformanceCounter();
    timefreq = SDL_GetPerformanceFrequency();

    lasttime = -0.02f;    // ensure first frame doesn't get a crazy delta

    return gl_err;
}

void SDLSetFullscreen(InitFlags flags) {
    int mode = 0;
    if (flags & INIT_FULLSCREEN) {
        if (flags & INIT_NATIVE) {
            // If you switch to fullscreen you get some random display mode that is not the
            // native res? So we have to find the native res first.
            const int display_in_use = 0;  // Only using first display
            int display_mode_count = SDL_GetNumDisplayModes(display_in_use);
            if (display_mode_count < 1) return;
            SDL_DisplayMode bestdm;
            int bestres = 0;
            for (int i = 0; i < display_mode_count; ++i) {
                SDL_DisplayMode dm;
                if (SDL_GetDisplayMode(display_in_use, i, &dm) != 0) return;
                int res = dm.w * dm.h;
                if (res > bestres) {
                    bestres = res;
                    bestdm = dm;
                }
            }
            // Set desired fullscreen res to highest res we found.
            SDL_SetWindowDisplayMode(_sdl_window, &bestdm);
            mode = SDL_WINDOW_FULLSCREEN;
        } else {
            mode = SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
    } else {
        if (flags & INIT_MAXIMIZED) {
            mode = SDL_WINDOW_MAXIMIZED;
        }
    }
    SDL_SetWindowFullscreen(_sdl_window, mode);
}

string SDLDebuggerWindow() {
    #ifdef PLATFORM_ES3
        return "Can\'t open debugger window on non-desktop platform";
    #endif
    if (!_sdl_debugger_context) {
        _sdl_debugger_window = SDL_CreateWindow(
            "Lobster Debugger", 100, SDL_WINDOWPOS_CENTERED, 600, 800,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                SDL_WINDOW_ALLOW_HIGHDPI);
        if (!_sdl_debugger_window)
            return "Debugger SDL_CreateWindow fail";
        _sdl_debugger_context = SDL_GL_CreateContext(_sdl_debugger_window);
        if (!_sdl_debugger_context)
            return "Debugger SDL_GL_CreateContext fail";
        return OpenGLInit(1, true);
    } else {
        SDL_GL_MakeCurrent(_sdl_debugger_window, _sdl_debugger_context);
        return {};
    }
}

void SDLDebuggerOff() {
    SDL_GL_MakeCurrent(_sdl_window, _sdl_context);
    // Reset since we may have interrupted a user interaction.
    for (auto &it : keymap) it.second.Reset();
}

bool SDLDebuggerFrame() {
    #ifndef __EMSCRIPTEN__
        SDL_GL_SwapWindow(_sdl_debugger_window);
    #endif
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        extern pair<bool, bool> IMGUIEvent(SDL_Event *event);
        IMGUIEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                return true;
            case SDL_WINDOWEVENT: 
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_CLOSE:
                        return true;
                }
                break;
        }
    }
    return false;
}

double GetSeconds() { return (double)(SDL_GetPerformanceCounter() - timestart) / (double)timefreq; }

void SDLShutdown() {
    // FIXME: SDL gives ERROR: wglMakeCurrent(): The handle is invalid. upon SDL_GL_DeleteContext
    if (_sdl_context) {
        //SDL_GL_DeleteContext(_sdl_context);
        _sdl_context = nullptr;
    }
    if (_sdl_window) {
        SDL_DestroyWindow(_sdl_window);
        _sdl_window = nullptr;
    }
    if (_sdl_debugger_context) {
        //SDL_GL_DeleteContext(_sdl_debugger_context);
        _sdl_debugger_context = nullptr;
    }
    if (_sdl_debugger_window) {
        SDL_DestroyWindow(_sdl_debugger_window);
        _sdl_debugger_window = nullptr;
    }

    SDL_Quit();
}

vector<float> &SDLGetFrameTimeLog() { return frametimelog; }

float SDLGetRollingAverage(size_t n) {
    n = std::max((size_t)1, std::min(frametimelog.size(), n));
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        sum += frametimelog[frametimelog.size() - n + i];
    }
    return sum / frametimelog.size();
}

bool SDLFrame() {
    if (minimized) {
        SDL_Delay(100);  // save CPU/battery
    } else {
        #ifndef __EMSCRIPTEN__
            SDL_GL_SwapWindow(_sdl_window);
            OpenGLPostSwapBuffers();
        #else
            emscripten_sleep(0);
        #endif
    }

    frametime = GetSeconds() - lasttime;
    lasttime += frametime;
    // Let's not run slower than this, very long pauses can cause animation & gameplay glitches.
    const double minfps = 5.0;
    frametime = min(1.0 / minfps, frametime);
    frames++;
    frametimelog.push_back((float)frametime);
    if (frametimelog.size() > 64) frametimelog.erase(frametimelog.begin());

    for (auto &it : keymap) it.second.FrameAdvance();

    mousewheeldelta = 0;
    clearfingers(true);
    dropped_file.clear();

    if (!cursor) clearfingers(false);

    bool closebutton = false;

    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        extern pair<bool, bool> IMGUIEvent(SDL_Event *event);
        auto nomousekeyb = IMGUIEvent(&event);
        switch(event.type) {
            case SDL_QUIT:
                closebutton = true;
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (nomousekeyb.second) break;
                const char *kn = SDL_GetKeyName(event.key.keysym.sym);
                if (!*kn) break;
                string name = kn;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](char c) { return (char)::tolower(c); });
                updatebutton(name, event.key.state==SDL_PRESSED, 0, event.key.repeat);
                if (event.type == SDL_KEYDOWN) {
                    // Built-in key-press functionality.
                    switch (event.key.keysym.sym) {
                        case SDLK_PRINTSCREEN:
                            ScreenShot("screenshot-" + GetDateTime() + ".png");
                            break;
                    }
                }
                break;
            }

            // This #ifdef is needed, because on e.g. OS X we'd otherwise get SDL_FINGERDOWN in addition to SDL_MOUSEBUTTONDOWN on laptop touch pads.
            #ifdef PLATFORM_TOUCH

            // FIXME: if we're in cursor==0 mode, only update delta, not position
            case SDL_FINGERDOWN: {
                if (nomousekeyb.first) break;
                int i = updatedragpos(event.tfinger, event.type);
                updatemousebutton(1, i, true);
                break;
            }
            case SDL_FINGERUP: {
                if (nomousekeyb.first) break;
                int i = findfinger(event.tfinger.fingerId, true);
                updatemousebutton(1, i, false);
                break;
            }

            case SDL_FINGERMOTION: {
                if (nomousekeyb.first) break;
                updatedragpos(event.tfinger, event.type);
                break;
            }

            #else

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                if (nomousekeyb.first) break;
                updatemousebutton(event.button.button, 0, event.button.state != 0);
                if (cursor) {
                    fingers[0].mousepos = int2(event.button.x, event.button.y) * inputscale;
                }
                break;
            }

            case SDL_MOUSEMOTION:
                if (nomousekeyb.first) break;
                fingers[0].mousedelta += int2(event.motion.xrel, event.motion.yrel);
                if (cursor) {
                    fingers[0].mousepos = int2(event.motion.x, event.motion.y) * inputscale;
                } else {
                    //if (skipmousemotion) { skipmousemotion--; break; }
                    //if (event.motion.x == screensize.x / 2 && event.motion.y == screensize.y / 2) break;

                    //auto delta = int3(event.motion.xrel, event.motion.yrel);
                    //fingers[0].mousedelta += delta;

                    //auto delta = int3(event.motion.x, event.motion.y) - screensize / 2;
                    //fingers[0].mousepos -= delta;

                    //SDL_WarpMouseInWindow(_sdl_window, screensize.x / 2, screensize.y / 2);
                }
                break;

            case SDL_MOUSEWHEEL: {
                if (nomousekeyb.first) break;
                if (event.wheel.which == SDL_TOUCH_MOUSEID) break;  // Emulated scrollwheel on touch devices?
                auto y = event.wheel.y;
                #ifdef __EMSCRIPTEN__
                    y = y > 0 ? 1 : -1;  // For some reason, it defaults to 10 / -10 ??
                #endif
                mousewheeldelta += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -y : y;
                break;
            }

            #endif

            case SDL_JOYAXISMOTION: {
                const int deadzone = 8192;
                if (event.jaxis.axis < MAXAXES) {
                    joyaxes[event.jaxis.axis] = abs(event.jaxis.value) > deadzone ? event.jaxis.value / (float)0x8000 : 0;
                };
                break;
            }

            case SDL_JOYHATMOTION:
                break;

            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP: {
                string name = "joy" + to_string(event.jbutton.button);
                updatebutton(name, event.jbutton.state == SDL_PRESSED, 0, false);
                break;
            }

            case SDL_CONTROLLERDEVICEADDED:
                if (!controller) {
                    controller = SDL_GameControllerOpen(event.cdevice.which);
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (controller && event.cdevice.which ==
                                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
                    SDL_GameControllerClose(controller);
                    controller = find_controller();
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                if (controller && event.cdevice.which ==
                                      SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
                    string name = "controller_";
                    auto sdl_name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)event.cbutton.button);
                    name += sdl_name ? sdl_name : cat(event.cbutton.button); 
                    updatebutton(name, event.cbutton.state == SDL_PRESSED, 0, false);
                }
                break;
            case SDL_CONTROLLERAXISMOTION: {
                const int deadzone = 8192;
                controller_axes[event.caxis.axis] =
                        abs(event.caxis.value) > deadzone ? event.caxis.value / (float)0x8000 : 0;
                break;
            }

            case SDL_WINDOWEVENT:
                LOG_DEBUG("SDL_WINDOWEVENT ", event.window.event);
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_RESIZED: {
                        ScreenSizeChanged();
                        // reload and bind shaders/textures here
                        break;
                    }
                    case SDL_WINDOWEVENT_MINIMIZED:
                    case SDL_WINDOWEVENT_HIDDEN:
                        minimized = true;
                        break;
                    case SDL_WINDOWEVENT_MAXIMIZED:
                    case SDL_WINDOWEVENT_RESTORED:
                    case SDL_WINDOWEVENT_SHOWN:
                        minimized = false;
                        break;
                    case SDL_WINDOWEVENT_EXPOSED:
                        // This event seems buggy, it fires right after SDL_WINDOWEVENT_MINIMIZED when the user minimizes?
                        break;
                    case SDL_WINDOWEVENT_LEAVE:
                        // never gets hit?
                        /*
                        for (int i = 1; i <= 5; i++)
                            updatemousebutton(i, false);
                        */
                        break;
                    case SDL_WINDOWEVENT_CLOSE:
                        closebutton = true;
                        break;
                }
                break;

            case SDL_DROPFILE:
                dropped_file = event.drop.file;
                SDL_free(event.drop.file);
                break;

            case SDL_TEXTINPUT:
                textinput.text += event.text.text;
                break;

            case SDL_TEXTEDITING:
                textinput.editing = event.edit.text;
                textinput.cursor = event.edit.start;
                textinput.len = event.edit.length;
                break;
        }
    }

    // simulate mouse up events, since SDL won't send any if the mouse leaves the window while down
    // doesn't work
    /*
    for (int i = 1; i <= 5; i++)
        if (!(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(i)))
            updatemousebutton(i, false);
    */

    /*
    if (SDL_GetMouseFocus() != _sdl_window) {
        int A = 1;
    }
    */

    TextToSpeechUpdate();

    return closebutton || (noninteractivetestmode && frames == 2 /* has rendered one full frame */);
}

void SDLWindowMinMax(int dir) {
    if (!_sdl_window) return;
    if (dir < 0) SDL_MinimizeWindow(_sdl_window);
    else if (dir > 0) SDL_MaximizeWindow(_sdl_window);
    else SDL_RestoreWindow(_sdl_window);
}

double SDLTime() { return lasttime; }
double SDLDeltaTime() { return frametime; }

pair<int64_t, int64_t> GetKS(string_view name) {
    auto &ks = keymap[string(name)];
    #ifdef PLATFORM_TOUCH
        // delayed results by one frame, that way they get 1 frame over finger hovering over target,
        // which makes gl_hit work correctly
        // FIXME: this causes more lag on mobile, instead, set a flag that this is the first frame we're touching,
        // and make that into a special case inside gl_hit
        return ks.Prev();
    #else
        return ks.State();
    #endif
}

bool KeyRepeat(string_view name) {
    auto &ks = keymap[string(name)];
    return ks.repeat;
}

double GetKeyTime(string_view name, int on) {
    auto &ks = keymap[string(name)];
    return ks.lasttime[on];
}

int2 GetKeyPos(string_view name, int on) {
    auto &ks = keymap[string(name)];
    return ks.lastpos[on];
}

void SDLTitle(string_view_nt title) {
    LOBSTER_FRAME_PROFILE_THIS_SCOPE;
    SDL_SetWindowTitle(_sdl_window, title.c_str());
}

int SDLWheelDelta() { return mousewheeldelta; }
bool SDLIsMinimized() { return minimized; }

bool SDLCursor(bool on) {
    if (on == cursor) return cursor;
    cursor = !cursor;
    if (cursor) {
        if (fullscreen) SDL_SetWindowGrab(_sdl_window, SDL_FALSE);
        SDL_ShowCursor(1);
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_WarpMouseInWindow(_sdl_window, cursorx, cursory);
    } else {
        SDL_GetMouseState(&cursorx, &cursory);
        if (fullscreen) SDL_SetWindowGrab(_sdl_window, SDL_TRUE);
        SDL_ShowCursor(0);
        SDL_SetRelativeMouseMode(SDL_TRUE);
        clearfingers(false);
    }
    return !cursor;
}

bool SDLGrab(bool on) {
    SDL_SetWindowGrab(_sdl_window, on ? SDL_TRUE : SDL_FALSE);
    return SDL_GetWindowGrab(_sdl_window) == SDL_TRUE;
}

void SDLMessageBox(string_view_nt title, string_view_nt msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), msg.c_str(), _sdl_window);
}

bool SDLOpenURL(string_view url) {
    #ifdef __ANDROID__
        return false;
    #else
        return SDL_OpenURL(url.data()) == 0;
    #endif
}

int64_t SDLLoadFile(string_view_nt absfilename, string *dest, int64_t start, int64_t len) {
    LOG_DEBUG("SDLLoadFile: ", absfilename);
    auto f = SDL_RWFromFile(absfilename.c_str(), "rb");
    if (!f) return -1;
    auto filelen = SDL_RWseek(f, 0, RW_SEEK_END);
    if (filelen < 0 || filelen == LLONG_MAX) {
        // If SDL_RWseek fails it is supposed to return -1, but on Linux it returns LLONG_MAX instead.
        SDL_RWclose(f);
        LOG_INFO("SDLLoadFile: ", absfilename, " failed to determine file size.");
        return -1;
    }
    if (!len) {  // Just the file length requested.
        SDL_RWclose(f);
        return filelen;
    }
    if (len < 0) len = filelen;
    SDL_RWseek(f, start, RW_SEEK_SET);
    dest->resize((size_t)len);
    auto rlen = SDL_RWread(f, &(*dest)[0], 1, (size_t)len);
    SDL_RWclose(f);
    if (len != (int64_t)rlen) {
        LOG_INFO("SDLLoadFile: ", absfilename, " file is not of requested length. requested: ", len, " found: ", rlen);
        return -1;
    }
    return  len;
}

bool ScreenShot(string_view_nt filename) {
    auto pixels = ReadPixels(int2(0), screensize);
    stbi_flip_vertically_on_write(0);
    auto ok = stbi_write_png(filename.c_str(), screensize.x, screensize.y, 3, pixels,
                             screensize.x * 3);
    delete[] pixels;
    return ok != 0;
}

void SDLTestMode() { noninteractivetestmode = true; }

int SDLScreenDPI(int screen) {
    int screens = max(1, SDL_GetNumVideoDisplays());
    float ddpi = 200;  // Reasonable default just in case screen 0 gives an error.
    #ifndef __EMSCRIPTEN__
    SDL_GetDisplayDPI(screen, &ddpi, nullptr, nullptr);
    #endif
    return screen >= screens
           ? 0  // Screen not present.
           : (int)(ddpi + 0.5f);
}

void SDLStartTextInput(int2 pos, int2 size) {
    SDL_StartTextInput();
    SDL_Rect rect = { pos.x, pos.y, size.x, size.y };
    SDL_SetTextInputRect(&rect);
    textinput = TextInput();
}

TextInput &SDLTextInputState() {
    return textinput;
}

void SDLTextInputSet(string_view t) {
    textinput.text = t;
}

void SDLEndTextInput() {
    SDL_StopTextInput();
}

const char *SDLControllerDataBase(const string& buf) {
    return SDL_GameControllerAddMapping(buf.c_str()) >= 0 ? nullptr : SDL_GetError();
}
