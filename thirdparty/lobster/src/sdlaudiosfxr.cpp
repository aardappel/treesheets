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

#include <atomic>

#include "lobster/natreg.h"

#include "lobster/sdlincludes.h"
#include "lobster/sdlinterface.h"

#include "SDL_mixer.h"
#include "SDL_stdinc.h"

static constexpr int INVALID_MUSIC = 0;

bool sound_init = false;

struct Sound {
    unique_ptr<Mix_Chunk, decltype(&Mix_FreeChunk)> chunk;
    Sound() : chunk(nullptr, Mix_FreeChunk) {}
    explicit Sound(Mix_Chunk *chunk) : chunk(chunk, Mix_FreeChunk) {}
};

struct Music {
    unique_ptr<Mix_Music, decltype(&Mix_FreeMusic)> music;
    unique_ptr<atomic<bool>> finished;  // Needs to be wrapped in unique_ptr so Music can be moved.
    Music() : music(nullptr, Mix_FreeMusic) {}
    explicit Music(Mix_Music *music) : music(music, Mix_FreeMusic), finished(make_unique<atomic<bool>>(false)) {}
};

const int channel_num = 16; // number of mixer channels
int sound_pri[channel_num] = {};
uint64_t sound_age[channel_num] = {};
uint64_t sounds_played = 0;

map<string, Sound, less<>> sound_files;

vector<Music> playing_music;

Mix_Chunk *AllocChunk(short *buf, size_t num_samples) {
    Uint16 format;
    Mix_QuerySpec(nullptr, &format, nullptr);
    if (format != AUDIO_S16SYS) {
        assert(false);
        return nullptr;
    }
    auto sbuf = SDL_malloc(num_samples * 2);
    memcpy(sbuf, buf, num_samples * 2);
    auto chunk = Mix_QuickLoad_RAW((Uint8 *)sbuf, (Uint32)num_samples * 2);
    chunk->allocated = 1;
    return chunk;
}

Mix_Chunk *RenderSFXR(string_view buf) {
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

    auto file = buf.data();
    auto fread_mem = [&](auto &dest) {
        assert(file < buf.data() + buf.size());
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
        auto ss = (Sint16)(sample * 0x7FFF);
        // FIXME: backwards way to make it 44100.
        // Instead, make it actually synth at that rate.
        if (!synth.empty()) synth.back() = (synth[synth.size() - 1] + ss) / 2;
        synth.push_back(ss);
        synth.push_back(0);
    }
    synth.pop_back();
    return AllocChunk(synth.data(), synth.size());
}

static Mix_Chunk *LoadSoundFromBuffer(string_view buf, SoundType st) {
    Mix_Chunk *chunk = nullptr;
    switch (st) {
        case SOUND_WAV:
        case SOUND_OGG: {
            auto rwops = SDL_RWFromMem((void *)buf.data(), (int)buf.length());
            if (!rwops) return nullptr;
            chunk = Mix_LoadWAV_RW(rwops, 1);
            break;
        }
        case SOUND_SFXR: {
            chunk = RenderSFXR(buf);
            break;
        }
    }
    if (!chunk) return nullptr;
    //Mix_VolumeChunk(chunk, MIX_MAX_VOLUME / 2);
    return chunk;
}

Mix_Chunk *LoadSound(string_view filename, SoundType st) {
    auto it = sound_files.find(filename);
    if (it != sound_files.end()) {
        return it->second.chunk.get();
    }
    string buf;
    if (LoadFile(filename, &buf) < 0) return {};
    auto *chunk = LoadSoundFromBuffer(buf, st);
    if (!chunk) return nullptr;
    sound_files.insert({ string(filename), Sound(chunk) });
    return chunk;
}

bool SDLSoundInit() {
    if (sound_init) return true;

    for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i) {
        LOG_INFO("Audio driver available ", SDL_GetAudioDriver(i));
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;

    #ifdef _WIN32
        // It defaults to wasapi which doesn't output any sound?
        auto err = SDL_AudioInit("directsound");
        if (err) LOG_INFO("Forcing driver failed", err);
    #endif

    int count = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < count; ++i) {
        LOG_INFO("Audio device ", i, ":", SDL_GetAudioDeviceName(i, 0));
    }

    Mix_Init(0);
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) == -1) {
        LOG_ERROR("Mix_OpenAudio: ", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(channel_num);
    // This seems to be needed to not distort when multiple sounds are played.
    Mix_Volume(-1, MIX_MAX_VOLUME / 2);
    sound_init = true;
    return true;
}

void SDLSoundClose() {
    sound_files.clear();
    for (auto &music: playing_music) {
        if (!*music.finished && music.music.get()) {
            Mix_HaltMusicStream(music.music.get());
        }
    }
    playing_music.clear();

    Mix_CloseAudio();
    while (Mix_Init(0)) Mix_Quit();

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

int SDLLoadSound(string_view filename, SoundType st) {
    if (!SDLSoundInit()) return 0;
    return LoadSound(filename, st) != nullptr;
}

int SDLLoadSoundFromBuffer(string_view buffer, SoundType st) {
    if (!SDLSoundInit()) return 0;
    return LoadSoundFromBuffer(buffer, st) != nullptr;
}

static int PlaySound(Mix_Chunk *chunk, float vol, int loops, int pri) {
    int ch = Mix_GroupAvailable(-1); // is there any free channel?
    if (ch == -1) {
        // no free channel -- find the lowest priority/oldest sound of all currently playing that are <= prio
        int p = pri;
        uint64_t sa = UINT64_MAX;
        for (int i = 0; i < channel_num; i++) {
            if (sound_pri[i] < p) {
                ch = i;
                p = sound_pri[i];
                sa = sound_age[i]; // save sound age too in case multiple channels equal this new priority!
            }
            else if (sound_pri[i] == p && sound_age[i] < sa) {
                ch = i;
                sa = sound_age[i];
            }
        }
        if (ch >= 0) Mix_HaltChannel(ch); // halt that channel
    }
    if (ch >= 0) {
        Mix_PlayChannel(ch, chunk, loops);
        Mix_Volume(ch, (int)(MIX_MAX_VOLUME * vol)); // set channel to default volume (max)
        sound_pri[ch] = pri; // add priority to our array
        sound_age[ch] = sounds_played++;
    }
    return ++ch; // we return channel numbers 1..8 rather than 0..7
}

int SDLPlaySound(string_view filename, SoundType st, float vol, int loops, int pri) {
    if (!SDLSoundInit()) return 0;
    auto *chunk = LoadSound(filename, st);
    if (!chunk) return 0;
    return PlaySound(chunk, vol, loops, pri);
}

int SDLPlaySoundFromBuffer(string_view buffer, SoundType st, float vol, int loops, int pri) {
    if (!SDLSoundInit()) return 0;
    auto *chunk = LoadSoundFromBuffer(buffer, st);
    if (!chunk) return 0;
    return PlaySound(chunk, vol, loops, pri);
}

void SDLHaltSound(int ch) {
    ch--; // SDL_Mixer enmerates channels from 0, Lobster from 1
    Mix_Resume(ch); // fix SDL bug not clearing the paused flag on halt by resuming (even if not paused) right before halt
    Mix_HaltChannel(ch);
}

void SDLPauseSound(int ch) {
    Mix_Pause(ch - 1);
}

int SDLSoundStatus(int ch) { // returns -1 for illegal channel index, 0 for available , 1 for playing, 2 for paused
    int num_chn = Mix_AllocateChannels(-1); // called with -1 this returns the current number of channels
    if (ch <= num_chn) return Mix_Playing(ch - 1) + Mix_Paused(ch - 1);
    else return -1;
}

float SDLGetTimeLength(int ch) {
    int num_chn = Mix_AllocateChannels(-1); // called with -1 this returns the current number of channels
    if (ch <= num_chn) {
        Mix_Chunk *chunk = Mix_GetChunk(ch - 1);
        return chunk->alen / (44100.f * 2 * 2);
    }
    else return 0.0f;
}

int SDLRegisterEffect(int ch, SDLEffectFunc func, SDLEffectDone done, void *userdata) {
    int num_chn = Mix_AllocateChannels(-1); // called with -1 this returns the current number of channels
    if (ch <= 0 || ch >= num_chn) return -1;
    return Mix_RegisterEffect(ch - 1, func, done, userdata);
}

void SDLResumeSound(int ch) {
    if (Mix_Paused(ch - 1) > 0) Mix_Resume(ch - 1); // this already tests for SDLSoundInit() etc.
}

void SDLSetVolume(int ch, float vol) {
    Mix_Volume(ch - 1, (int)(MIX_MAX_VOLUME * vol));
}

void SDLSetPosition(int ch, float3 vecfromlistener, float3 listenerfwd, float attnscale) {
    float dist = length(vecfromlistener) / attnscale;
    vecfromlistener = normalize(vecfromlistener);
    float angle = atan2(listenerfwd.y, listenerfwd.x) - atan2(vecfromlistener.y, vecfromlistener.x);
    Mix_SetPosition(ch - 1, int16_t(angle / RAD), uint8_t(1.0f + min(dist, 254.0f)));
}

// SDL Mixer Music 

static void MusicFinished(Mix_Music *, void *user_data) {
    int mus_idx = (int)(intptr_t)user_data;
    *playing_music[mus_idx].finished = true;
}

// Normally you can use SDL_RWFromFP, but we don't want to build SDL with STDIO
// support. So reimplement just the part we need here.
static Sint64 RWOpsFileSize(struct SDL_RWops * context) {
    FILE *fp = (FILE*)context->hidden.unknown.data1;
    if (!fp) return -1;
    auto orig_pos = ftell(fp);
    fseek(fp, 0, SEEK_END);
    auto endpos = ftell(fp);
    fseek(fp, orig_pos, SEEK_SET);
    return endpos;
}

static Sint64 RWOpsFileSeek(struct SDL_RWops * context, Sint64 offset, int whence) {
    FILE *fp = (FILE*)context->hidden.unknown.data1;
    if (!fp) return -1;
    switch (whence) {
        case RW_SEEK_SET:
            fseek(fp, (long)offset, SEEK_SET);
            break;
        case RW_SEEK_CUR:
            fseek(fp, (long)offset, SEEK_CUR);
            break;
        case RW_SEEK_END:
            fseek(fp, (long)offset, SEEK_END);
            break;
    }
    return ftell(fp);
}

static size_t RWOpsFileRead (struct SDL_RWops * context, void *ptr, size_t size, size_t maxnum) {
    FILE *fp = (FILE*)context->hidden.unknown.data1;
    if (!fp) return 0;
    return fread(ptr, size, maxnum, fp);
}

static size_t RWOpsFileWrite (struct SDL_RWops *, const void *, size_t, size_t) {
    // Always fail.
    return 0;
}

static int RWOpsFileClose (struct SDL_RWops * context) {
    FILE *fp = (FILE*)context->hidden.unknown.data1;
    if (!fp) return -1;
    fclose(fp);
    context->hidden.unknown.data1 = nullptr;
    return 0;
}

static SDL_RWops *CreateFileRWops(string_view filename) {
    FILE *file = OpenForReading(filename, true, true);
    if (!file) return nullptr;
    auto *rwops = SDL_AllocRW();
    rwops->hidden.unknown.data1 = file;
    rwops->size = RWOpsFileSize;
    rwops->seek = RWOpsFileSeek;
    rwops->read = RWOpsFileRead;
    rwops->write = RWOpsFileWrite;
    rwops->close = RWOpsFileClose;
    return rwops;
}

static int LoadMusic(string_view filename) {
    auto *rwops = CreateFileRWops(filename);
    if (!rwops) return INVALID_MUSIC;
    Mix_Music *mus = Mix_LoadMUS_RW(rwops, 1);
    if (!mus) return INVALID_MUSIC;
    // Look for any unused spots in the playing_music vector.
    int mus_idx = -1;
    for (int i = 0; i < (int)playing_music.size(); ++i) {
        if (*playing_music[i].finished) {
            mus_idx = i;
            playing_music[i] = Music(mus);
            break;
        }
    }
    if (mus_idx == -1) {
        // No empty slots found, append to the end.
        mus_idx = (int)playing_music.size();
        playing_music.push_back(Music(mus));
    }
    Mix_HookMusicStreamFinished(mus, MusicFinished, (void*)(intptr_t)mus_idx);
    return mus_idx + 1;  // We return music index where 0 is invalid.
}

static Mix_Music *GetMixMusic(int mus_id) {
    if (mus_id <= 0 || mus_id - 1 >= (int)playing_music.size()) {
        return nullptr;
    }
    return playing_music[mus_id - 1].music.get();
}

int SDLPlayMusic(string_view filename, int loops) {
    if (!SDLSoundInit()) return INVALID_MUSIC;
    int mus_id = LoadMusic(filename);
    if (mus_id == INVALID_MUSIC) return INVALID_MUSIC;
    int res = Mix_PlayMusicStream(GetMixMusic(mus_id), loops);
    if (res != 0) {
        LOG_ERROR("Mix_PlayMusicStream: ", Mix_GetError());
    }
    return mus_id;
}

int SDLFadeInMusic(string_view filename, int loops, int ms) {
    if (!SDLSoundInit()) return INVALID_MUSIC;
    int mus_id = LoadMusic(filename);
    if (mus_id == INVALID_MUSIC) return INVALID_MUSIC;
    int res = Mix_FadeInMusicStream(GetMixMusic(mus_id), loops, ms);
    if (res != 0) {
        LOG_ERROR("Mix_FadeInMusicStream: ", Mix_GetError());
    }
    return mus_id;
}

int SDLCrossFadeMusic(int old_mus_id, string_view new_filename, int loops, int ms) {
    if (!SDLSoundInit()) return INVALID_MUSIC;
    Mix_Music *old_mus = GetMixMusic(old_mus_id);
    if (old_mus == nullptr) return INVALID_MUSIC;

    int new_mus_id = LoadMusic(new_filename);
    if (new_mus_id == INVALID_MUSIC) return INVALID_MUSIC;
    int res = Mix_CrossFadeMusicStream(old_mus, GetMixMusic(new_mus_id), loops, ms, 0);
    if (res != 0) {
        LOG_ERROR("Mix_CrossFadeMusicStream: ", Mix_GetError());
    }
    return new_mus_id;
}

void SDLFadeOutMusic(int mus_id, int ms) {
    Mix_Music *mus = GetMixMusic(mus_id);
    if (mus == nullptr) return;
    Mix_FadeOutMusicStream(mus, ms);
}

void SDLHaltMusic(int mus_id) {
    Mix_Music *mus = GetMixMusic(mus_id);
    if (mus == nullptr) return;
    Mix_HaltMusicStream(mus);
}

void SDLPauseMusic(int mus_id) {
    Mix_Music *mus = GetMixMusic(mus_id);
    if (mus == nullptr) return;
    Mix_PauseMusicStream(mus);
}

void SDLResumeMusic(int mus_id) {
    Mix_Music *mus = GetMixMusic(mus_id);
    if (mus == nullptr) return;
    Mix_ResumeMusicStream(mus);
}

void SDLSetMusicVolume(int mus_id, float vol) {
    Mix_Music *mus = GetMixMusic(mus_id);
    if (mus == nullptr) return;
    Mix_VolumeMusicStream(mus, (int)(MIX_MAX_VOLUME * vol));
}

bool SDLMusicIsPlaying(int mus_id) {
    if (mus_id <= 0 || mus_id - 1 >= (int)playing_music.size()) {
        return false;
    }
    return !*playing_music[mus_id - 1].finished;
}

void SDLSetGeneralMusicVolume(float vol) {
    Mix_VolumeMusicGeneral((int)(MIX_MAX_VOLUME * vol));
}

#ifdef _MSC_VER
    #pragma warning(disable : 4244 4457 4245 4701)
#endif
