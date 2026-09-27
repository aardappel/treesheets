// Password protection of a document. The key is derived from the password with Argon2id once (when
// the password is set or the file is loaded), so saves don't pay for that again. Each save
// encrypts everything after the file header with XChaCha20-Poly1305 under a new random nonce, and
// authenticates the header along with it.
struct Encryption {
    static constexpr uint8_t method = 1;  // Argon2id + XChaCha20-Poly1305, stored in the header
    // The file header: "TSFF", version, method, nbblocks, nbpasses, salt, nonce.
    typedef array<uint8_t, 54> Header;
    static constexpr size_t saltpos = 14, noncepos = 30;

    uint32_t nbblocks {1 << 16};  // Argon2id memory use in KiB
    uint32_t nbpasses {3};
    array<uint8_t, 16> salt {};
    array<uint8_t, 32> key {};
    // Of the file being loaded.
    Header header {};
    array<uint8_t, 16> mac {};

    ~Encryption() { crypto_wipe(key.data(), key.size()); }

    static bool RandomBytes(uint8_t *buf, size_t len) {
        #ifdef _WIN32
            return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len),
                                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        #else
            for (size_t i = 0; i < len; i += 256) {  // getentropy's limit per call
                if (getentropy(buf + i, min<size_t>(len - i, 256)) != 0) { return false; }
            }
            return true;
        #endif
    }

    bool DeriveKey(const wxString &password) {
        size_t worksize = size_t {nbblocks} * 1024;
        unique_ptr<uint64_t[]> work(new (nothrow) uint64_t[worksize / sizeof(uint64_t)]);
        if (!work) { return false; }
        auto pass = password.utf8_str();
        crypto_argon2(key.data(), key.size(), work.get(), {CRYPTO_ARGON2_ID, nbblocks, nbpasses, 1},
                      {reinterpret_cast<const uint8_t *>(pass.data()), salt.data(),
                       static_cast<uint32_t>(pass.length()), static_cast<uint32_t>(salt.size())},
                      crypto_argon2_no_extras);
        crypto_wipe(work.get(), worksize);
        crypto_wipe(pass.data(), pass.length());
        return true;
    }

    static bool IsEncrypted(const wxString &filename) {
        wxFFileInputStream is(filename);
        char h[6];
        return is.IsOk() && is.Read(h, sizeof(h)).LastRead() == sizeof(h) &&
               !memcmp(h, "TSFF", 4) && h[4] >= 30 && h[5] == method;
    }

    // A new salt and key, for a new password.
    bool SetPassword(const wxString &password) {
        return RandomBytes(salt.data(), salt.size()) && DeriveKey(password);
    }

    // Writes the header after the version and method bytes, then the encrypted body.
    bool Write(wxOutputStream &os, vector<uint8_t> &body) const {
        Header h {'T', 'S', 'F', 'F', TS_VERSION, method};
        loop(i, 4) {
            h[6 + i] = static_cast<uint8_t>(nbblocks >> 8 * i);
            h[10 + i] = static_cast<uint8_t>(nbpasses >> 8 * i);
        }
        copy(salt.begin(), salt.end(), h.begin() + saltpos);
        if (!RandomBytes(h.data() + noncepos, h.size() - noncepos)) { return false; }
        uint8_t bodymac[16];
        crypto_aead_lock(body.data(), bodymac, key.data(), h.data() + noncepos, h.data(), h.size(),
                         body.data(), body.size());
        os.Write(h.data() + 6, h.size() - 6);
        os.Write(bodymac, sizeof(bodymac));
        os.Write(body.data(), body.size());
        return os.IsOk();
    }

    // Reads what follows the method byte of an encrypted file, and the encrypted body.
    bool Read(wxInputStream &is, char version, vector<uint8_t> &body) {
        header = {'T', 'S', 'F', 'F', static_cast<uint8_t>(version), method};
        is.Read(header.data() + 6, header.size() - 6);
        if (is.LastRead() != header.size() - 6) { return false; }
        is.Read(mac.data(), mac.size());
        if (is.LastRead() != mac.size()) { return false; }
        nbblocks = nbpasses = 0;
        loop(i, 4) {
            nbblocks |= uint32_t {header[6 + i]} << 8 * i;
            nbpasses |= uint32_t {header[10 + i]} << 8 * i;
        }
        // Rule out settings that would take all memory or forever.
        if (nbblocks < 8 || nbblocks > 1 << 21 || nbpasses < 1 || nbpasses > 64) { return false; }
        copy(header.begin() + saltpos, header.begin() + noncepos, salt.begin());
        auto len = is.GetLength() - is.TellI();
        if (len < 0) { return false; }
        body.resize(len);
        is.Read(body.data(), len);
        return is.LastRead() == static_cast<size_t>(len);
    }

    // Decrypts the body in place if the password is right; leaves it untouched otherwise.
    bool Decrypt(const wxString &password, vector<uint8_t> &body) {
        return DeriveKey(password) &&
               crypto_aead_unlock(body.data(), mac.data(), key.data(), header.data() + noncepos,
                                  header.data(), header.size(), body.data(), body.size()) == 0;
    }
};
