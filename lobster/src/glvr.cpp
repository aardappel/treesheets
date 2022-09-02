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
#include "lobster/natreg.h"

#include "lobster/glinterface.h"
#include "lobster/glincludes.h"

#include "lobster/sdlinterface.h"
#include "lobster/sdlincludes.h"

#include "lobster/graphics.h"

#ifdef PLATFORM_VR

#include "openvr.h"

static vr::IVRSystem *vrsys = nullptr;
static vr::IVRRenderModels *vrmodels = nullptr;
static vr::TrackedDevicePose_t trackeddeviceposes[vr::k_unMaxTrackedDeviceCount];
static map<string, vr::EVRButtonId, less<>> button_ids;

string GetTrackedDeviceString(vr::TrackedDeviceIndex_t device, vr::TrackedDeviceProperty prop) {
    assert(vrsys);
    uint32_t buflen = vrsys->GetStringTrackedDeviceProperty(device, prop, nullptr, 0, nullptr);
    if(buflen == 0) return "";
    char *buf = new char[buflen];
    buflen = vrsys->GetStringTrackedDeviceProperty(device, prop, buf, buflen, nullptr);
    std::string s = buf;
    delete [] buf;
    return s;
}

float4x4 FromOpenVR(const vr::HmdMatrix44_t &mat) { return float4x4(&mat.m[0][0]).transpose(); }

float4x4 FromOpenVR(const vr::HmdMatrix34_t &mat) {
    return float4x4(float4(&mat.m[0][0]),
                    float4(&mat.m[1][0]),
                    float4(&mat.m[2][0]),
                    float4(0, 0, 0, 1)).transpose();  // FIXME: simplify
}

#endif  // PLATFORM_VR

static int2 rtsize = int2_0;
static Texture mstex[2];
static Texture retex[2];
static float4x4 hmdpose = float4x4_1;
struct MotionController {
    float4x4 mat;
    uint32_t device;
    #ifdef PLATFORM_VR
        vr::VRControllerState_t state, laststate;
    #endif
    bool tracking;
};
static vector<MotionController> motioncontrollers;

void VRShutDown() {
    #ifdef PLATFORM_VR
    button_ids.clear();
    if (vrsys) vr::VR_Shutdown();
    vrsys = NULL;
    for (int i = 0; i < 2; i++) {
        DeleteTexture(mstex[i]);
        DeleteTexture(retex[i]);
    }
    #else
    (void)mstex;
    #endif  // PLATFORM_VR
}

bool VRInit() {
    #ifdef PLATFORM_VR
    if (vrsys) return true;
    extern bool noninteractivetestmode;
    if (noninteractivetestmode) return false;
    button_ids["system"]   = vr::k_EButton_System;
    button_ids["menu"]     = vr::k_EButton_ApplicationMenu;
    button_ids["grip"]     = vr::k_EButton_Grip;
    button_ids["touchpad"] = vr::k_EButton_SteamVR_Touchpad;
    button_ids["trigger"]  = vr::k_EButton_SteamVR_Trigger;
    if (!vr::VR_IsHmdPresent()) return false;
    vr::EVRInitError err = vr::VRInitError_None;
    vrsys = vr::VR_Init(&err, vr::VRApplication_Scene);
    if (err != vr::VRInitError_None) {
        vrsys = nullptr;
        LOG_ERROR("VR system init failed: ",
                             vr::VR_GetVRInitErrorAsEnglishDescription(err));
        return false;
    }
    vrsys->GetRecommendedRenderTargetSize((uint32_t *)&rtsize.x, (uint32_t *)&rtsize.y);
    auto devicename = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd,
                                             vr::Prop_TrackingSystemName_String);
    auto displayname = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd,
                                              vr::Prop_SerialNumber_String);
    LOG_INFO("VR running on device: \"", devicename, "\", display: \"", displayname,
                        "\", rt size: (", rtsize.x, ", ", rtsize.y, ")");
    vrmodels = (vr::IVRRenderModels *)vr::VR_GetGenericInterface(vr::IVRRenderModels_Version, &err);
    if(!vrmodels) {
        VRShutDown();
        LOG_ERROR("VR get render models failed: ",
                             vr::VR_GetVRInitErrorAsEnglishDescription(err));
        return false;
    }
    if (!vr::VRCompositor()) {
        VRShutDown();
        LOG_ERROR("VR compositor failed to initialize");
        return false;
    }
    // Get focus?
    vr::VRCompositor()->WaitGetPoses(trackeddeviceposes, vr::k_unMaxTrackedDeviceCount, NULL, 0);
    return true;
    #else
    (void)rtsize;
    return false;
    #endif  // PLATFORM_VR
}

void VRStart() {
    #ifdef PLATFORM_VR
    if (!vrsys) return;
    auto err = vr::VRCompositor()->WaitGetPoses(trackeddeviceposes, vr::k_unMaxTrackedDeviceCount,
                                                NULL, 0);
    (void)err;
    assert(!err);
    // Feels worse with this prediction?
    /*
    float fSecondsSinceLastVsync = 0;
    vrsys->GetTimeSinceLastVsync(&fSecondsSinceLastVsync, NULL);
    float fDisplayFrequency = vrsys->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                                   vr::Prop_DisplayFrequency_Float);
    float fFrameDuration = 1.f / fDisplayFrequency;
    float fVsyncToPhotons = vrsys->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                          vr::Prop_SecondsFromVsyncToPhotons_Float);
    float fPredictedSecondsFromNow = fFrameDuration - fSecondsSinceLastVsync + fVsyncToPhotons;
    vrsys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, fPredictedSecondsFromNow,
                                           trackeddeviceposes, vr::k_unMaxTrackedDeviceCount);
                                           */
    size_t mcn = 0;
    for (uint32_t device = 0; device < vr::k_unMaxTrackedDeviceCount; device++) {
        if (vrsys->GetTrackedDeviceClass(device) != vr::TrackedDeviceClass_Controller)
            continue;
        if (mcn == motioncontrollers.size()) {
            MotionController mc;
            memset(&mc, 0, sizeof(mc));
            motioncontrollers.push_back(mc);
        }
        auto &mc = motioncontrollers[mcn];
        mc.tracking = trackeddeviceposes[device].bPoseIsValid;
        mc.mat = mc.tracking
            ? FromOpenVR(trackeddeviceposes[device].mDeviceToAbsoluteTracking)
            : float4x4_1;
        mc.device = device;
        mc.laststate = mc.state;
        auto ok = vrsys->GetControllerState(device, &mc.state, sizeof(mc.state));
        if (!ok) memset(&mc.state, 0, sizeof(vr::VRControllerState_t));
        mcn++;
    }
    #endif  // PLATFORM_VR
}

void VREye(int eye, float znear, float zfar) {
    #ifdef PLATFORM_VR
    if (!vrsys) return;
    auto retf = TF_CLAMP | TF_NOMIPMAP;
    auto mstf = retf | TF_MULTISAMPLE;
    if (!mstex[eye].id) mstex[eye] = CreateBlankTexture("vr_mstex", rtsize, float4_0, mstf);
    if (!retex[eye].id) retex[eye] = CreateBlankTexture("vr_retex", rtsize, float4_0, retf);
    SwitchToFrameBuffer(mstex[eye], GetScreenSize(), true, mstf, retex[eye]);
    auto proj =
        FromOpenVR(vrsys->GetProjectionMatrix((vr::EVREye)eye, znear, zfar));
    Set3DMode(80, int2_0, GetScreenSize(), znear, zfar);
    view2clip = proj;  // Override the projection set by Set3DMode
    auto eye2head = FromOpenVR(vrsys->GetEyeToHeadTransform((vr::EVREye)eye));
    auto vrview = eye2head;
    if (trackeddeviceposes[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid) {
        hmdpose = FromOpenVR(
            trackeddeviceposes[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
        vrview = hmdpose * vrview;
    }
    otransforms.set_object2view(otransforms.object2view() * invert(vrview));
    #endif  // PLATFORM_VR
}

void VRFinish() {
    #ifdef PLATFORM_VR
    if (!vrsys) return;
    SwitchToFrameBuffer(Texture(), GetScreenSize());
    for (int i = 0; i < 2; i++) {
        vr::Texture_t vrtex = {
            (void *)(size_t)retex[i].id,
            vr::TextureType_OpenGL,
            vr::ColorSpace_Gamma
        };
        auto err = vr::VRCompositor()->Submit((vr::EVREye)i, &vrtex);
        (void)err;
        assert(!err);
    }
    // This should be after swap, but isn't needed if it is followed by WaitGetPoses anyway.
    //vr::VRCompositor()->PostPresentHandoff();
    #endif  // PLATFORM_VR
}

Mesh *VRCreateMesh(uint32_t device) {
    #ifdef PLATFORM_VR
    if (!vrsys) return 0;
    auto name = GetTrackedDeviceString(device, vr::Prop_RenderModelName_String);
    vr::RenderModel_t *model = nullptr;
    for (;;) {
        auto err = vr::VRRenderModels()->LoadRenderModel_Async(name.c_str(), &model);
        if (err == vr::VRRenderModelError_None) {
            break;
        } else if (err != vr::VRRenderModelError_Loading) {
            return 0;
        }
        SDL_Delay(1);
    }
    vr::RenderModel_TextureMap_t *modeltex = nullptr;
    for (;;) {
        auto err = vr::VRRenderModels()->LoadTexture_Async(model->diffuseTextureId, &modeltex);
        if (err == vr::VRRenderModelError_None) {
            break;
        } else if (err != vr::VRRenderModelError_Loading) {
            vr::VRRenderModels()->FreeRenderModel(model);
            return 0;
        }
        SDL_Delay(1);
    }
    auto tex = CreateTexture("vr_controller_tex", modeltex->rubTextureMapData,
                             int3(modeltex->unWidth, modeltex->unHeight, 0), TF_CLAMP);
    auto m = new Mesh(
        new Geometry("vr_controller_verts", gsl::make_span(model->rVertexData, model->unVertexCount),
                                   "PNT"), PRIM_TRIS);
    auto nindices = model->unTriangleCount * 3;
    vector<int> indices(nindices);
    for (uint32_t i = 0; i < nindices; i += 3) {
        indices[i + 0] = model->rIndexData[i + 0];
        indices[i + 1] = model->rIndexData[i + 2];
        indices[i + 2] = model->rIndexData[i + 1];
    }
    auto surf = new Surface("vr_controller_idxs", gsl::make_span(indices), PRIM_TRIS);
    surf->Get(0) = tex;
    m->surfs.push_back(surf);
    vr::VRRenderModels()->FreeRenderModel(model);
    vr::VRRenderModels()->FreeTexture(modeltex);
    return m;
    #else
    return nullptr;
    #endif  // PLATFORM_VR
}

using namespace lobster;

MotionController *GetMC(const Value &mc) {
    auto n = mc.ival();
    return n >= 0 && n < (int)motioncontrollers.size()
        ? &motioncontrollers[n]
        : nullptr;
};

#ifdef PLATFORM_VR

vr::EVRButtonId GetButtonId(VM &vm, Value &button) {
    auto it = button_ids.find(button.sval()->strv());
    if (it == button_ids.end())
        vm.BuiltinError("unknown button name: " + button.sval()->strv());
    return it->second;
}

#endif  // PLATFORM_VR

void AddVR(NativeRegistry &nfr) {

nfr("vr_init", "", "", "B",
    "initializes VR mode. returns true if a hmd was found and initialized",
    [](StackPtr &, VM &) {
        return Value(VRInit());
    });

nfr("vr_start_eye", "isright,znear,zfar", "IFF", "",
    "starts rendering for an eye. call for each eye, followed by drawing the world as normal."
    " replaces gl_perspective",
    [](StackPtr &, VM &, Value &isright, Value &znear, Value &zfar) {
        VREye(isright.True(), znear.fltval(), zfar.fltval());
        return NilVal();
    });

nfr("vr_start", "", "", "",
    "starts VR by updating hmd & controller poses",
    [](StackPtr &, VM &) {
        VRStart();
        return NilVal();
    });

nfr("vr_finish", "", "", "",
    "finishes vr rendering by compositing (and distorting) both eye renders to the screen",
    [](StackPtr &, VM &) {
        VRFinish();
        return NilVal();
    });

nfr("vr_set_eye_texture", "unit,isright", "II", "",
    "sets the texture for an eye (like gl_set_primitive_texture). call after vr_finish. can be"
    " used to render the non-VR display",
    [](StackPtr &, VM &vm, Value &unit, Value &isright) {
        extern int GetSampler(VM &vm, Value &i);
        SetTexture(GetSampler(vm, unit), retex[isright.True()]);
        return NilVal();
    });

nfr("vr_num_motion_controllers", "", "", "I",
    "returns the number of motion controllers in the system",
    [](StackPtr &, VM &) {
        return Value((int)motioncontrollers.size());
    });

nfr("vr_motioncontrollerstracking", "n", "I", "B",
    "returns if motion controller n is tracking",
    [](StackPtr &, VM &, Value &mc) {
        auto mcd = GetMC(mc);
        return Value(mcd && mcd->tracking);
    });

nfr("vr_motion_controller", "n", "I", "",
    "sets up the transform ready to render controller n."
    " if there is no controller n (or it is currently not"
    " tracking) the identity transform is used",
    [](StackPtr &sp, VM &) {
        auto mc = Pop(sp);
        auto mcd = GetMC(mc);
        otransforms.append_object2view(mcd ? mcd->mat : float4x4_1);
    });

nfr("vr_create_motion_controller_mesh", "n", "I", "R:mesh?",
    "returns the mesh for motion controller n, or nil if not available",
    [](StackPtr &, VM &vm, Value &mc) {
        auto mcd = GetMC(mc);
        extern ResourceType mesh_type;
        return mcd ? Value(vm.NewResource(&mesh_type, VRCreateMesh(mcd->device))) : NilVal();
    });

// TODO: make it return an "up" value much like gl_button, since this doesn't represent
// down+up in the same frame.
nfr("vr_motion_controller_button", "n,button", "IS", "I",
    "returns the button state for motion controller n."
    " isdown: >= 1, wentdown: == 1, wentup: == 0, isup: <= 0."
    " buttons are: system, menu, grip, trigger, touchpad",
    [](StackPtr &, VM &vm, Value &mc, Value &button) {
        #ifdef PLATFORM_VR
            auto mcd = GetMC(mc);
            auto mask = ButtonMaskFromId(GetButtonId(vm, button));
            if (!mcd) return Value(-1);
            auto masknow = mcd->state.ulButtonPressed & mask;
            auto maskbef = mcd->laststate.ulButtonPressed & mask;
            return Value(int(masknow != 0) * 2 - int(!(maskbef != 0)));
        #else
            return Value(0);
        #endif
    });

nfr("vr_motion_controller_vec", "n,i", "II", "F}:3",
    "returns one of the vectors for motion controller n. 0 = left, 1 = up, 2 = fwd, 4 = pos."
    " These are in Y up space.",
    [](StackPtr &sp, VM &vm) {
        auto idx = Pop(sp);
        auto mcd = GetMC(Pop(sp));
        if (!mcd) { PushVec(sp, float3_0); return; }
        auto i = RangeCheck(vm, idx, 4);
        PushVec(sp, mcd->mat[i].xyz());
    });

nfr("vr_hmd_vec", "i", "I", "F]:3",
    "returns one of the vectors for hmd pose. 0 = left, 1 = up, 2 = fwd, 4 = pos."
    " These are in Y up space.",
    [](StackPtr &sp, VM &vm) {
        auto i = RangeCheck(vm, Pop(sp), 4);
        PushVec(sp, hmdpose[i].xyz());
    });

}  // AddVR

