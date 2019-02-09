
// A basic 3D grid, with individually allocated YZ arrays.
// Make Z your inner loop when accessing this.
template<typename T> class Chunk3DGrid : NonCopyable {
    public:
    int3 dim;

    private:
    vector<T *> grid;

    T &Access(const int3 &pos) const { return grid[pos.x][pos.y * dim.z + pos.z]; }

    public:
    Chunk3DGrid(const int3 &_dim, T default_val) : dim(_dim) {
        grid.resize(dim.x, nullptr);
        for (int i = 0; i < dim.x; i++) {
            auto len = dim.y * dim.z;
            grid[i] = new T[len];
            std::fill_n(grid[i], len, default_val);
        }
    }

    ~Chunk3DGrid() {
        for (auto p : grid) delete[] p;
    }

    T &Get(const int3 &pos) const {
        return Access(pos);
    }

    // This creates a Z-major continuous buffer, whereas the original data is X-major.
    void ToContinousGrid(T *buf) {
        for (int z = 0; z < dim.z; z++) {
            for (int y = 0; y < dim.y; y++) {
                for (int x = 0; x < dim.x; x++) {
                    buf[z * dim.x * dim.y + y * dim.x + x] = Get(int3(x, y, z));
                }
            }
        }
    }
};

// Stores an XY grid of RLE Z lists, based on the value of T.
// Caches the current location in the list, so as long as you iterate through this with +Z as your
// inner loop, will be close to the efficiency of accessing a 3D grid, while using less memory and
// smaller blocks of it. Individual lists are reallocated as splitting of ranges makes this
// necessary.
template<typename T> class RLE3DGrid : NonCopyable {
    public:
    int3 dim;

    private:
    union RLEItem { T val; int count; };  // FIXME: bad if not the same size.
    vector<RLEItem *> grid;
    SlabAlloc alloc;
    T default_val;
    int2 cur_pos;
    RLEItem *cur, *it;
    int it_z;

    int CurSize() { return cur[0].count; }
    RLEItem *&GridLoc(const int2 &p) { return grid[p.x + p.y * dim.x]; }

    void Iterate(const int3 &pos) {
        auto p = pos.xy();
        if (p != cur_pos) {
            // We're switching to a new xy. Look up a new RLE list, and cache it.
            cur_pos = p;
            auto &rle = GridLoc(p);
            if (!rle) {  // Lazyly allocate lists.
                // A RLE list is a count of RLE pairs, first of which is a count, second the value.
                rle = alloc.alloc_array<RLEItem>(3);
                rle[0].count = 3;
                rle[1].count = dim.z;
                rle[2].val = default_val;
            }
            cur = rle;
            it = cur + 1;
            it_z = 0;
        } else if (pos.z < it_z) {
            // Iterating to an earlier part of the list (very uncommon).
            it_z = 0;
            it = cur + 1;
        }
        // Iterate towards correct part of the list (common case, usually skips while-body).
        while (pos.z >= it_z + it[0].count) {
            it_z += it[0].count;
            it += 2;
            assert(it < cur + CurSize());  // Should not run off end of list.
        }
        // Here, it[1] now has the correct value for "pos".
    }

    template<typename F> void CopyCurrent(int change, const int3 &pos, F f) {
        auto rle = alloc.alloc_array<RLEItem>(CurSize() + change);
        memcpy(rle, cur, (it - cur) * sizeof(RLEItem));
        rle[0].count += change;
        auto nit = rle + (it - cur);
        f(nit);
        memcpy(nit + 2 + change, it + 2, (CurSize() - (it - cur + 2)) * sizeof(RLEItem));
        alloc.dealloc_array(cur, CurSize());
        GridLoc(pos.xy()) = rle;
        cur = rle;
        it = nit;
    }

    public:
    RLE3DGrid(const int3 &_dim, T _default_val)
        : dim(_dim), default_val(_default_val), cur_pos(-1, -1), cur(nullptr), it(nullptr),
          it_z(0) {
        grid.resize(dim.x * dim.y, nullptr);
    }

    ~RLE3DGrid() {
    }

    T Get(const int3 &pos) {
        Iterate(pos);
        return it[1].val;
    }

    void Set(const int3 &pos, T newval) {
        Iterate(pos);
        // No change, we're done, yay!
        if (it[1].val == newval) return;
        if (it[0].count == 1) {
            // We can just overwrite.
            it[1].val = newval;
            return;
        }
        // Part of a larger range.
        if (it_z == pos.z) {
            // If this was the first value of a range..
            if (it > cur + 1 && it[-1].val == newval) {
                // ..and the preceding range has that value, we can simply shift the two ranges.
                it[-2].count++;
                it[0].count--;
                it_z++;
            } else {
                // Otherwise split in two.
                CopyCurrent(2, pos, [&](RLEItem *nit) {
                    nit[0].count = 1;
                    nit[1].val = newval;
                    nit[2].count = it[0].count - 1;
                    nit[3].val = it[1].val;
                });
            }
        } else if (it_z + it[0].count - 1 == pos.z) {
            // Similarly, if this is the last value of the range..
            if (dim.z > it_z + it[0].count && it[3].val == newval) {
                // ..and the next range has the new value, we can also shift these ranges.
                it[2].count++;
                it[0].count--;
            } else {
                // Otherwise split in two.
                CopyCurrent(2, pos, [&](RLEItem *nit) {
                    nit[0].count = it[0].count - 1;
                    nit[1].val = it[1].val;
                    nit[2].count = 1;
                    nit[3].val = newval;
                });
            }
        } else {
            // Split in 3.
            CopyCurrent(4, pos, [&](RLEItem *nit) {
                nit[0].count = pos.z - it_z;
                nit[1].val = it[1].val;
                nit[2].count = 1;
                nit[3].val = newval;
                nit[4].count = it[0].count - nit[0].count - 1;
                nit[5].val = it[1].val;
            });
        }
    }
};

