#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

// Embedded payload store: the newest game modules are packed (encrypted + Ed25519
// signed) into the loader binary at build time (see tools/payload_packer.cpp and
// CMakeLists.txt). At inject time each blob is signature-verified against the
// build machine's public key and decrypted in memory; plaintext exists only for
// the duration of the injection and is streamed straight into the target's memfd
// via inject_memfd's stdin mode - no plaintext file is ever written to disk.
//
// Crypto hygiene:
//  - libcrypto is dlopen'ed + dlsym'ed at runtime (NOT linked): the EVP_* symbols stay
//    OUT of the import table, so the dynamic symbol section does not narrate the design.
//    Any resolution failure = fail-closed (no extraction).
//  - The container magic is XOR-obfuscated (same trick as the session trailer): no
//    "NSPX" literal exists in the binary.
//  - The Ed25519 signature is verified BEFORE any decryption (it covers the whole
//    container), so the streaming decryptor can hand out plaintext chunks immediately.
//  - Decryption is CHUNKED (64 KB): there is never a full multi-megabyte plaintext image
//    in this process's address space - a /proc/<pid>/mem sweep for ELF headers finds
//    nothing usable.
namespace payloads
{

// Which embedded payloads exist in this build (determined at link time).
[[nodiscard]] bool hasCs2();
[[nodiscard]] bool hasTf2();
[[nodiscard]] bool hasSteamModule();
[[nodiscard]] bool hasInjector();

// Whole-buffer extraction (Ed25519 verify + ChaCha20-Poly1305 decrypt). Empty + error
// message on any failure (fail-closed). Used for the small injector payload and the gdb
// fallbacks; the game-module injection paths use the streaming API below.
[[nodiscard]] QByteArray extractCs2(QString *error = nullptr);
[[nodiscard]] QByteArray extractTf2(QString *error = nullptr);
[[nodiscard]] QByteArray extractSteamModule(QString *error = nullptr);
[[nodiscard]] QByteArray extractInjector(QString *error = nullptr);

// Streaming decryption: opens a verified payload and hands out plaintext chunks on
// demand. The signature is verified in open(); each nextChunk() decrypts in place and
// the caller must consume (write) the chunk before requesting the next one. The AEAD
// tag is checked when the last chunk is produced (belt-and-suspenders - the signature
// already proved the ciphertext).
//
// Internal note: members are public so the same-TU factory can build the object; treat
// the fields as opaque.
class PayloadStream
{
public:
    // Use the open*Stream() factories - constructing directly is not supported.
    PayloadStream() = default;
    ~PayloadStream();

    [[nodiscard]] qint64 plainSize() const noexcept { return m_plainLen; }

    // Decrypts the next chunk into `out` (at most 64 KB). Returns false when the stream
    // is exhausted (tag verified) or on error (error set, fail-closed).
    bool nextChunk(QByteArray &out, QString *error = nullptr);

    // internal state - same-TU factory builds this; treat as opaque
    const unsigned char *m_ciphertext = nullptr;
    qint64 m_ctRemaining = 0;
    qint64 m_plainLen = 0;
    qint64 m_plainProduced = 0;
    void *m_ctx = nullptr;
    bool m_finished = false;
};

struct StreamDeleter {
    void operator()(PayloadStream *s) const noexcept;
};
using StreamPtr = std::unique_ptr<PayloadStream, StreamDeleter>;

[[nodiscard]] StreamPtr openCs2Stream(QString *error = nullptr);
[[nodiscard]] StreamPtr openTf2Stream(QString *error = nullptr);
[[nodiscard]] StreamPtr openSteamStream(QString *error = nullptr);

} // namespace payloads
