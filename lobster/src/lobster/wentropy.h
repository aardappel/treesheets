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

// Wouter's Entropy Coder: apparently I re-invented adaptive Shannon-Fano.
//
// similar compression performance to huffman, and absolutely tiny code, one function for both
// compression and decompression. Adaptive, so should work well even for tiny buffers.
// One of the vectors passed in is the input, the other the output (and exactly one of the two
// should be empty initially).
// not the fastest possible implementation (only 40MB/sec for either compression or decompression
// on a modern pc), but should be sufficient for many uses
//
// uses std::string and std::swap as its only external dependencies for simplicity, but could made
// to not rely on them relatively easily.

template<bool compress> void WEntropyCoder(const unsigned char *in,
                                           size_t inlen,    // Size of input.
                                           size_t origlen,  // Uncompressed size.
                                           string &out) {
    const int NSYM = 256;     // This depends on the fact we're reading from unsigned chars.
    int symbol[NSYM];         // The symbol in this slot. Adaptively sorted by frequency.
    size_t freq[NSYM];        // Its frequency.
    int sym_idx[NSYM];        // Lookup symbol -> index into the above two arrays.
    for (int i = 0; i < NSYM; i++) { freq[i] = 1; symbol[i] = sym_idx[i] = i; }
    size_t compr_idx = 0;
    unsigned char bits = 0;
    int nbits = 0;
    for (size_t i = 0; i < origlen; i++) {
        int start = 0, range = NSYM;
        size_t total_freq = i + NSYM;
        while (range > 1) {
            size_t acc_freq = 0;
            int j = start;
            do acc_freq += freq[j++]; while (acc_freq + freq[j] / 2 < total_freq / 2);
            unsigned char bit = 0;
            if (compress) {
                if (sym_idx[in[i]] < j) bit = 1;
                bits |= bit << nbits;
                if (++nbits == 8) { out.push_back(bits); bits = 0; nbits = 0; }
            } else {
                if (!nbits) { assert(compr_idx < inlen); bits = in[compr_idx++]; nbits = 8; }
                bit = bits & 1;
                bits >>= 1;
                nbits--;
            }
            if (bit) {
                total_freq = acc_freq; assert(j - start < range); range = j - start;
            } else {
                total_freq -= acc_freq; range -= j - start; start = j;
            }
        }
        if (!compress) out.push_back((unsigned char)symbol[start]);
        assert(range == 1 && (!compress || in[i] == symbol[start]));
        freq[start]++;
        while (start && freq[start - 1] < freq[start]) {
            swap(sym_idx[symbol[start - 1]], sym_idx[symbol[start]]);
            swap(freq[start - 1], freq[start]);
            swap(symbol[start - 1], symbol[start]);
            start--;
        }
    }
    if (compress) { if (nbits) out.push_back(bits); } else { assert(compr_idx == inlen); }
    (void)inlen;
}

