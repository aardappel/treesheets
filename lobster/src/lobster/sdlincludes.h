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

#define DECLSPEC
#define SDL_NO_COMPAT
// hack: so we can share one include folder for SDL
#ifdef __APPLE__
    #ifdef __IOS__
        #include "SDL_config_iphoneos.h"
    #else
        #include "SDL_config_macosx.h"
    #endif
#else
    #ifdef __ANDROID__
        #include "SDL_config_android.h"
    #else
        #ifdef _WIN32
            #include "SDL_config_windows.h"
        #else
            #include "SDL_config.h"
        #endif
    #endif
#endif
#define _SDL_config_h
//#define SDL_MAIN_HANDLED
#include "SDL.h"
//#include "SDL_main.h"

