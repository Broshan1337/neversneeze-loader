// payload_packer - build-time host tool for the Neversnooze loader.
//
// Packs the newest game modules into encrypted + signed blobs that the loader
// embeds and verifies at inject time. Not shipped; exists only on the build
// machine.
//
//   payload_packer keygen <dir>
//       Creates (if missing) <dir>/payload_private.pem (Ed25519, NEVER shipped),
//       <dir>/payload_sym.key (32B symmetric key, embedded in the loader), and
//       prints the raw public key.
//
//   payload_packer pack <input> <output.blob> <keydir> <name>
//       Encrypts <input> with ChaCha20-Poly1305 (random nonce) and signs the
//       whole container with the Ed25519 private key.
//
// Container format (all integers little-endian):
//   "NSPX" | u32 version=1 | u32 nameLen | name | u64 ctLen | u64 plainLen
//   | nonce[12] | ciphertext[ctLen] (Poly1305 tag is the last 16 bytes)
//   | ed25519 signature[64] over every preceding byte.
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

static void die(const char* what)
{
    std::fprintf(stderr, "payload_packer: %s\n", what);
    std::exit(1);
}

static std::vector<unsigned char> readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        die("cannot open input file");
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static void writeFile(const char* path, const void* data, size_t size)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        die("cannot open output file");
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!f)
        die("write failed");
}

static void putU32(std::vector<unsigned char>& v, unsigned x)
{
    v.push_back(x & 0xff);
    v.push_back((x >> 8) & 0xff);
    v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 24) & 0xff);
}

static void putU64(std::vector<unsigned char>& v, unsigned long long x)
{
    for (int i = 0; i < 8; ++i)
        v.push_back(static_cast<unsigned char>((x >> (8 * i)) & 0xff));
}

static EVP_PKEY* loadPrivateKey(const std::string& keyDir)
{
    const std::string path = keyDir + "/payload_private.pem";
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f)
        die("cannot open private key (run `payload_packer keygen` first)");
    EVP_PKEY* pkey = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
    std::fclose(f);
    if (!pkey || EVP_PKEY_id(pkey) != EVP_PKEY_ED25519)
        die("private key is not an Ed25519 key");
    return pkey;
}

static void loadSymKey(const std::string& keyDir, unsigned char out[32])
{
    const std::string path = keyDir + "/payload_sym.key";
    std::ifstream f(path, std::ios::binary);
    if (!f)
        die("cannot open symmetric key (run `payload_packer keygen` first)");
    f.read(reinterpret_cast<char*>(out), 32);
    if (f.gcount() != 32)
        die("symmetric key is not 32 bytes");
}

// The session-binding key (stamped into every injected module as a trailer; the modules
// carry the same key masked - SessionBindKey.h in both module trees). Auto-created on
// first use so an older keydir keeps working.
static void loadHeartbeatKey(const std::string& keyDir, unsigned char out[32])
{
    const std::string path = keyDir + "/heartbeat.key";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        unsigned char fresh[32];
        if (RAND_bytes(fresh, sizeof(fresh)) != 1)
            die("RAND_bytes failed");
        writeFile(path.c_str(), fresh, sizeof(fresh));
        std::memcpy(out, fresh, 32);
        return;
    }
    f.read(reinterpret_cast<char*>(out), 32);
    if (f.gcount() != 32)
        die("heartbeat key is not 32 bytes");
}

static void appendHex(std::string& out, const unsigned char* data, size_t size)
{
    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0xf]);
    }
}

static void printHex(const unsigned char* data, size_t size)
{
    std::string s;
    appendHex(s, data, size);
    std::printf("%s\n", s.c_str());
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
            "usage:\n"
            "  payload_packer keygen <keydir>\n"
            "  payload_packer headers <keydir> <out-header>\n"
            "  payload_packer pack <input> <output.blob> <keydir> <name>\n");
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "keygen") {
        if (argc != 3)
            die("keygen takes one argument: <keydir>");
        const std::string dir = argv[2];
        MKDIR(dir.c_str()); // EEXIST is fine

        const std::string privPath = dir + "/payload_private.pem";
        FILE* probe = std::fopen(privPath.c_str(), "r");
        if (probe) {
            std::fclose(probe);
            std::printf("private key already exists at %s - leaving it untouched\n", privPath.c_str());
            return 0;
        }

        EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
        if (!pkey)
            die("EVP_PKEY_Q_keygen failed");
        FILE* f = std::fopen(privPath.c_str(), "w");
        if (!f || !PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
            std::fclose(f);
            die("cannot write private key");
        }
        std::fclose(f);

        unsigned char pub[32];
        size_t pubLen = sizeof(pub);
        if (!EVP_PKEY_get_raw_public_key(pkey, pub, &pubLen) || pubLen != 32)
            die("cannot extract public key");
        EVP_PKEY_free(pkey);

        unsigned char sym[32];
        if (RAND_bytes(sym, sizeof(sym)) != 1)
            die("RAND_bytes failed");
        writeFile((dir + "/payload_sym.key").c_str(), sym, sizeof(sym));

        unsigned char hb[32];
        if (RAND_bytes(hb, sizeof(hb)) != 1)
            die("RAND_bytes failed");
        writeFile((dir + "/heartbeat.key").c_str(), hb, sizeof(hb));

        std::printf("private key: %s\n", privPath.c_str());
        std::printf("public key:  ");
        printHex(pub, pubLen);
        return 0;
    }

    if (cmd == "pack") {
        if (argc != 6)
            die("pack takes: <input> <output.blob> <keydir> <name>");
        const std::string input = argv[2];
        const std::string output = argv[3];
        const std::string keyDir = argv[4];
        const std::string name = argv[5];

        const std::vector<unsigned char> plain = readFile(input.c_str());

        unsigned char inputSha[32];
        SHA256(plain.data(), plain.size(), inputSha);
        std::printf("input sha256: ");
        printHex(inputSha, sizeof(inputSha));
        EVP_PKEY* pkey = loadPrivateKey(keyDir);
        unsigned char symKey[32];
        loadSymKey(keyDir, symKey);

        // container header (everything except the ciphertext, nonce and sig)
        std::vector<unsigned char> header;
        header.insert(header.end(), {'N', 'S', 'P', 'X'});
        putU32(header, 1);
        putU32(header, static_cast<unsigned>(name.size()));
        header.insert(header.end(), name.begin(), name.end());
        putU64(header, plain.size() + 16); // ciphertext = plaintext + poly1305 tag
        putU64(header, plain.size());

        unsigned char nonce[12];
        if (RAND_bytes(nonce, sizeof(nonce)) != 1)
            die("RAND_bytes failed");

        std::vector<unsigned char> ct(plain.size() + 16);
        int len = 0, total = 0;
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx
            || EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, symKey, nonce) != 1
            || EVP_EncryptUpdate(ctx, nullptr, &len, header.data(), static_cast<int>(header.size())) != 1
            || (plain.empty()
                    || EVP_EncryptUpdate(ctx, ct.data(), &len, plain.data(), static_cast<int>(plain.size())) != 1)
            || EVP_EncryptFinal_ex(ctx, ct.data() + plain.size(), &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            die("encryption failed");
        }
        total = static_cast<int>(plain.size()) + len;
        unsigned char tag[16];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            die("cannot obtain AEAD tag");
        }
        EVP_CIPHER_CTX_free(ctx);
        // ChaCha20 is a stream cipher: Final adds no bytes, `total` == plaintext length.
        // The tag goes AFTER the ciphertext (the loader reads it from there).
        for (int i = 0; i < 16; ++i)
            ct[total + i] = tag[i];

        // message to sign = header + nonce + ciphertext + tag (the full container
        // content that precedes the signature)
        std::vector<unsigned char> message = header;
        message.insert(message.end(), nonce, nonce + sizeof(nonce));
        message.insert(message.end(), ct.begin(), ct.end());

        EVP_MD_CTX* md = EVP_MD_CTX_new();
        size_t sigLen = 64;
        std::vector<unsigned char> sig(64);
        if (!md
            || EVP_DigestSignInit(md, nullptr, nullptr, nullptr, pkey) != 1
            || EVP_DigestSign(md, sig.data(), &sigLen, message.data(), message.size()) != 1
            || sigLen != 64) {
            EVP_MD_CTX_free(md);
            die("signing failed");
        }
        EVP_MD_CTX_free(md);
        EVP_PKEY_free(pkey);

        std::vector<unsigned char> blob = message;
        blob.insert(blob.end(), sig.begin(), sig.end());
        writeFile(output.c_str(), blob.data(), blob.size());
        std::printf("packed %s -> %s (%zu bytes, name \"%s\")\n", input.c_str(), output.c_str(),
            blob.size(), name.c_str());
        return 0;
    }

    if (cmd == "headers") {
        // Emit the loader-side key header: the Ed25519 public key (signature
        // verification) plus the symmetric key XOR-masked with a per-generation random
        // mask, so the raw key bytes are not a literal in the shipped binary.
        if (argc != 4)
            die("headers takes: <keydir> <out-header>");
        const std::string keyDir = argv[2];
        const std::string outHeader = argv[3];

        EVP_PKEY* pkey = loadPrivateKey(keyDir);
        unsigned char pub[32];
        size_t pubLen = sizeof(pub);
        if (!EVP_PKEY_get_raw_public_key(pkey, pub, &pubLen) || pubLen != 32)
            die("cannot extract public key");
        EVP_PKEY_free(pkey);

        unsigned char sym[32];
        loadSymKey(keyDir, sym);
        unsigned char hb[32];
        loadHeartbeatKey(keyDir, hb);
        unsigned char mask[32];
        if (RAND_bytes(mask, sizeof(mask)) != 1)
            die("RAND_bytes failed");

        std::string out;
        out += "#pragma once\n\n";
        out += "#include <cstdint>\n\n";
        out += "// Generated by payload_packer - do not edit.\n";
        out += "namespace payload_keys {\n\n";
        out += "inline constexpr unsigned char kEd25519Public[32] = {";
        for (int i = 0; i < 32; ++i)
            out += (i ? "," : "") + std::to_string(pub[i]);
        out += "};\n\n";
        out += "inline constexpr unsigned char kSymKeyMasked[32] = {";
        for (int i = 0; i < 32; ++i)
            out += (i ? "," : "") + std::to_string(sym[i] ^ mask[i]);
        out += "};\n\n";
        out += "inline constexpr unsigned char kHeartbeatKeyMasked[32] = {";
        for (int i = 0; i < 32; ++i)
            out += (i ? "," : "") + std::to_string(hb[i] ^ mask[i]);
        out += "};\n\n";
        out += "inline constexpr unsigned char kSymKeyMask[32] = {";
        for (int i = 0; i < 32; ++i)
            out += (i ? "," : "") + std::to_string(mask[i]);
        out += "};\n\n";
        out += "inline constexpr unsigned char kHeartbeatKeyMask[32] = {";
        for (int i = 0; i < 32; ++i)
            out += (i ? "," : "") + std::to_string(mask[i]);
        out += "};\n\n";
        out += "} // namespace payload_keys\n";
        writeFile(outHeader.c_str(), out.data(), out.size());
        std::printf("wrote %s\n", outHeader.c_str());
        return 0;
    }

    die("unknown command");
}
