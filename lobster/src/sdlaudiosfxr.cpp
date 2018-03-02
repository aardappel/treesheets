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

#include "lobster/sdlincludes.h"

#include "SDL_mixer.h"
#include "SDL_stdinc.h"

bool sound_init = false;

struct Sound {
    unique_ptr<Mix_Chunk, decltype(&Mix_FreeChunk)> chunk;
    Sound() : chunk(nullptr, Mix_FreeChunk) {}
};

unordered_map<string, Sound> sound_files;

Mix_Chunk *RenderSFXR(const string &buf) {
    int wave_type = 0;

    float p_base_freq = 0.3f;
    float p_freq_limit = 0.0f;
    float p_freq_ramp = 0.0f;
    float p_freq_dramp = 0.0f;
    float p_duty = 0.0f;
    float p_duty_ramp = 0.0f;

    float p_vib_strength = 0.0f;
    float p_vib_speed = 0.0f;
    float p_vib_delay = 0.0f;

    float p_env_attack = 0.0f;
    float p_env_sustain = 0.3f;
    float p_env_decay = 0.4f;
    float p_env_punch = 0.0f;

    bool filter_on = false;
    float p_lpf_resonance = 0.0f;
    float p_lpf_freq = 1.0f;
    float p_lpf_ramp = 0.0f;
    float p_hpf_freq = 0.0f;
    float p_hpf_ramp = 0.0f;

    float p_pha_offset = 0.0f;
    float p_pha_ramp = 0.0f;

    float p_repeat_speed = 0.0f;

    float p_arp_speed = 0.0f;
    float p_arp_mod = 0.0f;

    float master_vol=0.05f;

    float sound_vol=0.5f;

    int phase;
    double fperiod;
    double fmaxperiod;
    double fslide;
    double fdslide;
    int period;
    float square_duty;
    float square_slide;
    int env_stage;
    int env_time;
    int env_length[3];
    float env_vol;
    float fphase;
    float fdphase;
    int iphase;
    float phaser_buffer[1024];
    int ipp;
    float noise_buffer[32];
    float fltp;
    float fltdp;
    float fltw;
    float fltw_d;
    float fltdmp;
    float fltphp;
    float flthp;
    float flthp_d;
    float vib_phase;
    float vib_speed;
    float vib_amp;
    int rep_time;
    int rep_limit;
    int arp_time;
    int arp_limit;
    double arp_mod;

    auto file = buf.c_str();
    auto fread_mem = [&](auto &dest) {
        memcpy(&dest, file, sizeof(dest));
        file += sizeof(dest);
    };
    int version = 0;
    fread_mem(version);
    if(version!=102)
        return nullptr;
    fread_mem(wave_type);
    fread_mem(sound_vol);
    fread_mem(p_base_freq);
    fread_mem(p_freq_limit);
    fread_mem(p_freq_ramp);
    fread_mem(p_freq_dramp);
    fread_mem(p_duty);
    fread_mem(p_duty_ramp);
    fread_mem(p_vib_strength);
    fread_mem(p_vib_speed);
    fread_mem(p_vib_delay);
    fread_mem(p_env_attack);
    fread_mem(p_env_sustain);
    fread_mem(p_env_decay);
    fread_mem(p_env_punch);
    fread_mem(filter_on);
    fread_mem(p_lpf_resonance);
    fread_mem(p_lpf_freq);
    fread_mem(p_lpf_ramp);
    fread_mem(p_hpf_freq);
    fread_mem(p_hpf_ramp);
    fread_mem(p_pha_offset);
    fread_mem(p_pha_ramp);
    fread_mem(p_repeat_speed);
    fread_mem(p_arp_speed);
    fread_mem(p_arp_mod);

    auto frnd = [](float range) -> float {
        return (float)(rand()%(10000+1))/10000*range;
    };

    auto ResetSample = [&](bool restart) {
        if(!restart)
            phase=0;
        fperiod=100.0/(p_base_freq*p_base_freq+0.001);
        period=(int)fperiod;
        fmaxperiod=100.0/(p_freq_limit*p_freq_limit+0.001);
        fslide=1.0-pow((double)p_freq_ramp, 3.0)*0.01;
        fdslide=-pow((double)p_freq_dramp, 3.0)*0.000001;
        square_duty=0.5f-p_duty*0.5f;
        square_slide=-p_duty_ramp*0.00005f;
        if(p_arp_mod>=0.0f)
            arp_mod=1.0-pow((double)p_arp_mod, 2.0)*0.9;
        else
            arp_mod=1.0+pow((double)p_arp_mod, 2.0)*10.0;
        arp_time=0;
        arp_limit=(int)(pow(1.0f-p_arp_speed, 2.0f)*20000+32);
        if(p_arp_speed==1.0f)
            arp_limit=0;
        if(!restart) {
            // reset filter
            fltp=0.0f;
            fltdp=0.0f;
            fltw=pow(p_lpf_freq, 3.0f)*0.1f;
            fltw_d=1.0f+p_lpf_ramp*0.0001f;
            fltdmp=5.0f/(1.0f+pow(p_lpf_resonance, 2.0f)*20.0f)*(0.01f+fltw);
            if(fltdmp>0.8f) fltdmp=0.8f;
            fltphp=0.0f;
            flthp=pow(p_hpf_freq, 2.0f)*0.1f;
            flthp_d=1.0f+p_hpf_ramp*0.0003f;
            // reset vibrato
            vib_phase=0.0f;
            vib_speed=pow(p_vib_speed, 2.0f)*0.01f;
            vib_amp=p_vib_strength*0.5f;
            // reset envelope
            env_vol=0.0f;
            env_stage=0;
            env_time=0;
            env_length[0]=(int)(p_env_attack*p_env_attack*100000.0f);
            env_length[1]=(int)(p_env_sustain*p_env_sustain*100000.0f);
            env_length[2]=(int)(p_env_decay*p_env_decay*100000.0f);

            fphase=pow(p_pha_offset, 2.0f)*1020.0f;
            if(p_pha_offset<0.0f) fphase=-fphase;
            fdphase=pow(p_pha_ramp, 2.0f)*1.0f;
            if(p_pha_ramp<0.0f) fdphase=-fdphase;
            iphase=abs((int)fphase);
            ipp=0;
            for(int i=0;i<1024;i++)
                phaser_buffer[i]=0.0f;

            for(int i=0;i<32;i++)
                noise_buffer[i]=frnd(2.0f)-1.0f;

            rep_time=0;
            rep_limit=(int)(pow(1.0f-p_repeat_speed, 2.0f)*20000+32);
            if(p_repeat_speed==0.0f)
                rep_limit=0;
        }
    };

    auto SynthSample = [&](int length, float* buffer) -> int {
        for(int i=0;i<length;i++) {
            rep_time++;
            if(rep_limit!=0 && rep_time>=rep_limit) {
                rep_time=0;
                ResetSample(true);
            }

            // frequency envelopes/arpeggios
            arp_time++;
            if(arp_limit!=0 && arp_time>=arp_limit) {
                arp_limit=0;
                fperiod*=arp_mod;
            }
            fslide+=fdslide;
            fperiod*=fslide;
            if(fperiod>fmaxperiod) {
                fperiod=fmaxperiod;
                if(p_freq_limit>0.0f) {
                    return i;
                }
            }
            float rfperiod=(float)fperiod;
            if(vib_amp>0.0f) {
                vib_phase+=vib_speed;
                rfperiod=float(fperiod*(1.0+sin(vib_phase)*vib_amp));
            }
            period=(int)rfperiod;
            if(period<8) period=8;
            square_duty+=square_slide;
            if(square_duty<0.0f) square_duty=0.0f;
            if(square_duty>0.5f) square_duty=0.5f;
            // volume envelope
            env_time++;
            if(env_time>env_length[env_stage]) {
                env_time=0;
                env_stage++;
                if(env_stage==3) {
                    return i;
                }
            }
            if(env_stage==0)
                env_vol=(float)env_time/env_length[0];
            if(env_stage==1)
                env_vol=1.0f+pow(1.0f-(float)env_time/env_length[1], 1.0f)*2.0f*p_env_punch;
            if(env_stage==2)
                env_vol=1.0f-(float)env_time/env_length[2];

            // phaser step
            fphase+=fdphase;
            iphase=abs((int)fphase);
            if(iphase>1023) iphase=1023;

            if(flthp_d!=0.0f) {
                flthp*=flthp_d;
                if(flthp<0.00001f) flthp=0.00001f;
                if(flthp>0.1f) flthp=0.1f;
            }

            float ssample=0.0f;
            for(int si=0;si<8;si++) {  // 8x supersampling
                float sample=0.0f;
                phase++;
                if(phase>=period) {
                    //				phase=0;
                    phase%=period;
                    if(wave_type==3)
                        for(int i=0;i<32;i++)
                            noise_buffer[i]=frnd(2.0f)-1.0f;
                }
                // base waveform
                float fp=(float)phase/period;
                switch(wave_type) {
                    case 0: // square
                        if(fp<square_duty)
                            sample=0.5f;
                        else
                            sample=-0.5f;
                        break;
                    case 1: // sawtooth
                        sample=1.0f-fp*2;
                        break;
                    case 2: // sine
                        sample=(float)sin(fp*2*PI);
                        break;
                    case 3: // noise
                        sample=noise_buffer[phase*32/period];
                        break;
                }
                // lp filter
                float pp=fltp;
                fltw*=fltw_d;
                if(fltw<0.0f) fltw=0.0f;
                if(fltw>0.1f) fltw=0.1f;
                if(p_lpf_freq!=1.0f) {
                    fltdp+=(sample-fltp)*fltw;
                    fltdp-=fltdp*fltdmp;
                } else {
                    fltp=sample;
                    fltdp=0.0f;
                }
                fltp+=fltdp;
                // hp filter
                fltphp+=fltp-pp;
                fltphp-=fltphp*flthp;
                sample=fltphp;
                // phaser
                phaser_buffer[ipp&1023]=sample;
                sample+=phaser_buffer[(ipp-iphase+1024)&1023];
                ipp=(ipp+1)&1023;
                // final accumulation and envelope application
                ssample+=sample*env_vol;
            }
            ssample=ssample/8*master_vol;

            ssample*=2.0f*sound_vol;

            if(buffer!=nullptr) {
                if(ssample>1.0f) ssample=1.0f;
                if(ssample<-1.0f) ssample=-1.0f;
                *buffer++=ssample;
            }
        }
        return length;
    };

    ResetSample(false);
    vector<Sint16> synth;
    for (;;) {
        float sample;
        auto gen = SynthSample(1, &sample);
        if (!gen) break;
        synth.push_back((Sint16)(sample * 0x7FFF));
    }
    Uint16 format;
    Mix_QuerySpec(nullptr, &format, nullptr);
    if (format != AUDIO_S16SYS) {
        assert(false);
        return nullptr;
    }
    auto sbuf = SDL_malloc(synth.size() * 2);
    memcpy(sbuf, synth.data(), synth.size() * 2);
    auto chunk = Mix_QuickLoad_RAW((Uint8 *)sbuf, (Uint32)synth.size() * 2);
    chunk->allocated = 1;
    return chunk;
}

Sound *LoadSound(const char* filename, bool sfxr) {
    auto it = sound_files.find(filename);
    if (it != sound_files.end()) {
        return &it->second;
    }
    string buf;
    if (LoadFile(filename, &buf) < 0)
        return nullptr;
    Mix_Chunk *chunk;
    if (!sfxr) {
        auto rwops = SDL_RWFromMem((void *)buf.c_str(), (int)buf.length());
        if (!rwops) return nullptr;
        chunk = Mix_LoadWAV_RW(rwops, 1);
    } else {
        chunk = RenderSFXR(buf);
    }
    if (!chunk) return nullptr;
    //Mix_VolumeChunk(chunk, MIX_MAX_VOLUME / 2);
    Sound snd;
    snd.chunk.reset(chunk);
    return &(sound_files[filename] = std::move(snd));
}

bool SDLSoundInit() {
    if (sound_init) return true;

    for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i) {
        Output(OUTPUT_INFO, "Audio driver available %s", SDL_GetAudioDriver(i));
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;

    #ifdef _WIN32
        // It defaults to wasapi which doesn't output any sound?
        auto err = SDL_AudioInit("directsound");
        if (err) Output(OUTPUT_INFO, "Forcing driver failed %d", err);
    #endif

    int count = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < count; ++i) {
        Output(OUTPUT_INFO, "Audio device %d: %s", i, SDL_GetAudioDeviceName(i, 0));
    }

    Mix_Init(0);
    // For some reason this distorts when set to 44100 and samples at 22050 are played.
    // Also SFXR seems hard-coded to 22050, so that's what we'll use for now.
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 1024) == -1) {
        Output(OUTPUT_ERROR, "Mix_OpenAudio: %s\n", Mix_GetError());
        return false;
    }
    // This seems to be needed to not distort when multiple sounds are played.
    Mix_Volume(-1, MIX_MAX_VOLUME / 2);
    sound_init = true;
    return true;
}

void SDLSoundClose() {
    sound_files.clear();

    Mix_CloseAudio();
    while (Mix_Init(0)) Mix_Quit();

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SDLPlaySound(const char *filename, bool sfxr, int vol) {
    #ifdef __EMSCRIPTEN__
    // Distorted in firefox and no audio at all in chrome, disable for now.
    return false;
    #endif

    if (!SDLSoundInit())
        return false;

    auto snd = LoadSound(filename, sfxr);
    if (snd) {
        Mix_Volume(Mix_PlayChannel(-1, snd->chunk.get(), 0), vol);
    }
    return !!snd;
}
