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

#ifndef LOBSTER_CUBEGEN_H
#define LOBSTER_CUBEGEN_H

const uint8_t transparant = 0;

struct Palette {
    vector<byte4> colors;
    uint64_t hash = 0;
};

const size_t default_palette_idx = 0;
const size_t normal_palette_idx = 1;

extern vector<Palette> palettes;

struct Voxels {
    size_t palette_idx;
    bool chunks_skipped = false;
    Chunk3DGrid<uint8_t> grid;
    int idx = 0;

    Voxels(const int3 &dim, size_t idx) : palette_idx(idx), grid(dim, transparant) {}

    template<typename F> void Do(const int3 &p, const int3 &sz, F f) {
        for (int x = max(0, p.x); x < min(p.x + sz.x, grid.dim.x); x++) {
            for (int y = max(0, p.y); y < min(p.y + sz.y, grid.dim.y); y++) {
                for (int z = max(0, p.z); z < min(p.z + sz.z, grid.dim.z); z++) {
                    auto pos = int3(x, y, z);
                    f(pos, grid.Get(pos));
                }
            }
        }
    }

    void Set(const int3 &p, const int3 &sz, uint8_t pi) {
        Do(p, sz, [&](const int3 &, uint8_t &vox) { vox = pi; });
    }

    void Copy(const int3 &p, const int3 &sz, const int3 &dest, const int3 &flip) {
        Do(p, sz, [&](const int3 &pos, uint8_t &vox) {
            auto d = (pos - p) * flip + dest;
            if (d >= int3_0 && d < grid.dim) grid.Get(d) = vox;
        });
    }

    void Clone(const int3 &p, const int3 &sz, Voxels *dest) {
        assert(dest->grid.dim == sz);
        Do(p, sz, [&](const int3 &pos, uint8_t &vox) {
            dest->grid.Get(pos - p) = vox;
        });
    }

    uint8_t Color2Palette(const float4 &color) const {
        if (color.w < 0.5f) return transparant;
        if (palette_idx == default_palette_idx) {  // Fast path.
            auto ic = byte4((int4(quantizec(color)) + (0x33 / 2)) / 0x33);
            // For some reason the palette has red where black should be??
            if (!ic.x && !ic.y && !ic.z) return 255;
            return (5 - ic.x) * 36 +
                    (5 - ic.y) * 6 +
                    (5 - ic.z) + 1;
        }
        float error = 999999;
        uint8_t pi = transparant;
        auto &palette = palettes[palette_idx].colors;
        for (size_t i = 1; i < palette.size(); i++) {
            auto err = squaredlength(color2vec(palette[i]) - color);
            if (err < error) { error = err; pi = (uint8_t)i; }
        }
        return pi;
    }

    size_t2 MemoryUsage() {
        // FIXME: does NOT account for shared palettes.
        return { sizeof(Voxels) + grid.dim.volume() + grid.dim.x * sizeof(void *), 0 };
    }
};

namespace lobster {

extern ResourceType voxel_type;

inline Voxels &GetVoxels(const Value &res) {
    return *GetResourceDec<Voxels *>(res, &voxel_type);
}

Value CubesFromMeshGen(VM &vm, const DistGrid &grid, int targetgridsize, int zoffset);

extern const unsigned int default_palette[256];

}

#endif  // LOBSTER_CUBEGEN_H
