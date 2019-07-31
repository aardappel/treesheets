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

template<typename T> struct OcTree {
    vector<T> nodes;
    vector<int> freelist;
    int world_bits;
    enum {
        OCTREE_SUBDIV = 8,  // This one is kind of a given..
        PARENT_INDEX = OCTREE_SUBDIV,
        ELEMENTS_PER_NODE = OCTREE_SUBDIV + 1,  // Last is parent pointer.
        ROOT_INDEX = 1  // Such that index 0 means "no parent".
    };

    OcTree(int bits) : nodes(ROOT_INDEX + ELEMENTS_PER_NODE), world_bits(bits) {
        for (auto &n : nodes) n.SetLeafData(0);
        nodes[ROOT_INDEX + OCTREE_SUBDIV].SetNodeIdx(0);  // Root doesn't have a parent.
    }

    int ToParent(int i) { return i - ((i - ROOT_INDEX) % ELEMENTS_PER_NODE); }
    int Deref(int children) { return nodes[children + PARENT_INDEX].NodeIdx(); }

    void Set(const int3 &pos, T val) {
        int cur = ROOT_INDEX;
        for (auto bit = world_bits - 1; ; bit--) {
            auto size = 1 << bit;
            auto off = pos & size;
            auto bv = off >> bit;
            auto ccur = cur + dot(bv, int3(1, 2, 4));
            auto oval = nodes[ccur];
            if (oval == val) return;
            if (bit) {  // Not at bottom yet.
                if (oval.IsLeaf()) {  // Values are not equal, so we must subdivide.
                    int ncur;
                    T parent;
                    parent.SetNodeIdx(ccur);
                    if (freelist.empty()) {
                        ncur = (int)nodes.size();
                        for (int i = 0; i < OCTREE_SUBDIV; i++) nodes.push_back(oval);
                        nodes.push_back(parent);
                    } else {
                        ncur = freelist.back();
                        freelist.pop_back();
                        for (int i = 0; i < OCTREE_SUBDIV; i++) nodes[ncur + i] = oval;
                        nodes[ncur + OCTREE_SUBDIV] = parent;
                    }
                    nodes[ccur].SetNodeIdx(ncur);
                    cur = ncur;
                } else {
                    cur = oval.NodeIdx();
                }
            } else {  // Bottom level.
                assert(val.IsLeaf());
                nodes[ccur] = val;
                // Try to merge this level all the way to the top.
                for (int pbit = 1; pbit < world_bits; pbit++) {
                    auto parent = Deref(cur);
                    auto children = nodes[parent].NodeIdx();
                    for (int i = 1; i < OCTREE_SUBDIV; i++) {  // If all 8 are the same..
                        if (nodes[children] != nodes[children + i]) return;
                    }
                    // Merge.
                    nodes[parent] = nodes[children];
                    freelist.push_back(children);
                    cur = ToParent(parent);
                }
                return;
            }
        }
    }

    pair<int, int> Get(const int3 &pos) {
        int bit = world_bits;
        int i = Get(pos, bit, ROOT_INDEX);
        return make_pair(i, bit);
    }

    int Get(const int3 &pos, int &bit, int cur) {
        for (;;) {
            // FIXME: dedup from Set.
            bit--;
            auto off = pos & (1 << bit);
            auto bv = off >> bit;
            auto ccur = cur + dot(bv, int3(1, 2, 4));
            auto oval = nodes[ccur];
            if (oval.IsLeaf()) {
                return ccur;
            } else {
                cur = oval.NodeIdx();
            }
        }
    }

    // This function is only needed when creating nodes without Set, as Set already merges
    // on the fly.
    T Merge(int cur = ROOT_INDEX) {
        for (int i = 0; i < OCTREE_SUBDIV; i++) {
            auto &n = nodes[cur + i];
            if (!n.IsLeaf()) n = Merge(n.NodeIdx());
        }
        T ov;
        ov.SetNodeIdx(cur);
        if (!nodes[cur].IsLeaf()) return ov;
        for (int i = 1; i < OCTREE_SUBDIV; i++) {
            if (nodes[cur] != nodes[cur + i]) return ov;
        }
        if (cur != ROOT_INDEX) {
            freelist.push_back(cur);
            ov = nodes[cur];
        }
        return ov;
    }
};

class OcVal {
    int32_t node;
  public:
    bool IsLeaf() const { return node < 0; }
    int32_t NodeIdx() { assert(node >= 0); return node; }
    void SetNodeIdx(int32_t n) { assert(n >= 0); node = n; }
    // leaf data is a 31-bit usigned integer.
    int32_t LeafData() { assert(node < 0); return node & 0x7FFFFFFF; }
    void SetLeafData(int32_t v) { assert(v >= 0); node = v | 0x80000000; }
    bool operator==(const OcVal &o) const { return node == o.node; }
    bool operator!=(const OcVal &o) const { return node != o.node; }
};
