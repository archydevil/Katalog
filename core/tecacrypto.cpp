/*LICENCE
    This file is part of Katalog

    Copyright (C) 2020, the Katalog Development team

    Author: Stephane Couturier (Symbioxy)

    Katalog is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Katalog is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Katalog; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/*FILE DESCRIPTION
/////////////////////////////////////////////////////////////////////////////
// Application: Katalog
// File Name:   tecacrypto.cpp
// Purpose:     Encrypted face and people layers (fork only, D44): the
//              primitives on OpenSSL's libcrypto and the layer container.
//              The format is documented at the top of tecacrypto.h.
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/tecacrypto.h"

#include <QCryptographicHash>
#include <QThread>
#include <QtEndian>

#include <openssl/opensslv.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
#include <openssl/thread.h>
#endif

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

namespace {

// ---- Small helpers

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

const unsigned char *bytes(const QByteArray &data)
{
    return reinterpret_cast<const unsigned char *>(data.constData());
}

unsigned char *bytes(char *data)
{
    return reinterpret_cast<unsigned char *>(data);
}

// The first libcrypto error of this thread; the queue is emptied
QString opensslReason()
{
    const unsigned long first = ERR_get_error();
    char text[256] = {};
    if (first)
        ERR_error_string_n(first, text, sizeof text);
    ERR_clear_error();
    return QString::fromLatin1(text);
}

QString failure(const char *what)
{
    const QString reason = opensslReason();
    return reason.isEmpty() ? QString::fromLatin1(what)
                            : QStringLiteral("%1 (%2)").arg(QLatin1String(what), reason);
}

template <typename T, void (*Free)(T *)>
struct Deleter {
    void operator()(T *p) const { Free(p); }
};
using CipherPtr    = std::unique_ptr<EVP_CIPHER, Deleter<EVP_CIPHER, EVP_CIPHER_free>>;
using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, Deleter<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>>;
using PkeyPtr      = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;
using PkeyCtxPtr   = std::unique_ptr<EVP_PKEY_CTX, Deleter<EVP_PKEY_CTX, EVP_PKEY_CTX_free>>;
using KdfPtr       = std::unique_ptr<EVP_KDF, Deleter<EVP_KDF, EVP_KDF_free>>;
using KdfCtxPtr    = std::unique_ptr<EVP_KDF_CTX, Deleter<EVP_KDF_CTX, EVP_KDF_CTX_free>>;

// Wipes a local secret when it goes out of scope
struct WipeOnExit {
    QByteArray &secret;
    ~WipeOnExit() { TecaCrypto::wipe(secret); }
};

// Large buffers go through EVP in pieces: its lengths are int
constexpr qsizetype kChunk = 1 << 30;

// Threads for Argon2id: as many as lanes, within the machine and a sane cap.
// OpenSSL runs Argon2id threads only once its pool allows them
// (OSSL_set_max_threads); the thread count never changes the result.
quint32 argon2Threads(quint32 lanes)
{
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
    const quint32 machine = quint32(std::max(1, QThread::idealThreadCount()));
    const quint32 wanted  = std::max<quint32>(1, std::min<quint32>({lanes, machine, 16u}));
    if (wanted <= 1)
        return 1;
    static std::mutex poolMutex;
    std::lock_guard<std::mutex> lock(poolMutex);
    if (OSSL_get_max_threads(nullptr) < wanted)
        OSSL_set_max_threads(nullptr, wanted);
    const uint64_t allowed = OSSL_get_max_threads(nullptr);
    return allowed >= 1 ? quint32(std::min<uint64_t>(wanted, allowed)) : 1;
#else
    Q_UNUSED(lanes)
    return 1;
#endif
}

// AES-256-GCM into caller buffers (out: size bytes, tag: kTagSize bytes)
bool gcmEncryptRaw(const QByteArray &key, const QByteArray &nonce,
                   const char *in, qsizetype size, const QByteArray &aad,
                   char *out, char *tag, QString *error)
{
    if (key.size() != TecaCrypto::kKeySize || nonce.size() != TecaCrypto::kNonceSize) {
        setError(error, QStringLiteral("AES-256-GCM needs a 32-byte key and a 12-byte nonce"));
        return false;
    }
    CipherPtr cipher(EVP_CIPHER_fetch(nullptr, "AES-256-GCM", nullptr));
    CipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!cipher || !ctx
        || EVP_EncryptInit_ex2(ctx.get(), cipher.get(), bytes(key), bytes(nonce), nullptr) != 1) {
        setError(error, failure("AES-256-GCM is not available"));
        return false;
    }
    int written = 0;
    for (qsizetype done = 0; done < aad.size(); done += kChunk) {
        const int piece = int(std::min(aad.size() - done, kChunk));
        if (EVP_EncryptUpdate(ctx.get(), nullptr, &written, bytes(aad) + done, piece) != 1) {
            setError(error, failure("AES-256-GCM failed on the associated data"));
            return false;
        }
    }
    qsizetype total = 0;
    for (qsizetype done = 0; done < size; done += kChunk) {
        const int piece = int(std::min(size - done, kChunk));
        written = 0;
        if (EVP_EncryptUpdate(ctx.get(), bytes(out) + total, &written,
                              reinterpret_cast<const unsigned char *>(in) + done, piece) != 1) {
            setError(error, failure("AES-256-GCM encryption failed"));
            return false;
        }
        total += written;
    }
    written = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), bytes(out) + total, &written) != 1
        || total + written != size) {
        setError(error, failure("AES-256-GCM encryption failed"));
        return false;
    }
    OSSL_PARAM params[2] = {
        OSSL_PARAM_construct_octet_string(OSSL_CIPHER_PARAM_AEAD_TAG, tag, TecaCrypto::kTagSize),
        OSSL_PARAM_construct_end()
    };
    if (EVP_CIPHER_CTX_get_params(ctx.get(), params) != 1) {
        setError(error, failure("AES-256-GCM tag not available"));
        return false;
    }
    return true;
}

// ---- Layer container (format at the top of tecacrypto.h)

const char       kMagic[8]      = {'T', 'E', 'C', 'A', 'L', 'A', 'Y', 'R'};
constexpr int    kFixedSize     = 40;                  // magic .. salt
constexpr int    kRecipientSize = 32 + 32 + 12 + 48;   // 124
constexpr int    kTrailerSize   = 12 + 8 + 32;         // payload nonce, length, digest
constexpr int    kWrappedSize   = TecaCrypto::kKeySize + TecaCrypto::kTagSize;
constexpr quint8 kKdfArgon2id   = 1;
const char       kWrapLabel[]   = "Teca layer wrap v1";
const char       kFingerprintLabel[] = "Teca reader key v1";

void putU8(QByteArray &b, quint8 v) { b.append(char(v)); }
void putU16(QByteArray &b, quint16 v) { char t[2]; qToBigEndian(v, t); b.append(t, 2); }
void putU32(QByteArray &b, quint32 v) { char t[4]; qToBigEndian(v, t); b.append(t, 4); }
void putU64(QByteArray &b, quint64 v) { char t[8]; qToBigEndian(v, t); b.append(t, 8); }

struct Reader {
    const QByteArray &data;
    qsizetype pos = 0;
    bool ok = true;

    bool need(qsizetype n)
    {
        if (ok && (n < 0 || data.size() - pos < n))
            ok = false;
        return ok;
    }
    quint8 u8() { return need(1) ? quint8(data.at(pos++)) : 0; }
    quint16 u16()
    {
        if (!need(2)) return 0;
        const quint16 v = qFromBigEndian<quint16>(data.constData() + pos);
        pos += 2;
        return v;
    }
    quint32 u32()
    {
        if (!need(4)) return 0;
        const quint32 v = qFromBigEndian<quint32>(data.constData() + pos);
        pos += 4;
        return v;
    }
    quint64 u64()
    {
        if (!need(8)) return 0;
        const quint64 v = qFromBigEndian<quint64>(data.constData() + pos);
        pos += 8;
        return v;
    }
    QByteArray take(qsizetype n)
    {
        if (!need(n)) return QByteArray();
        const QByteArray v = data.mid(pos, n);
        pos += n;
        return v;
    }
};

struct Recipient {
    QByteArray fingerprint;
    QByteArray ephemeral;
    QByteArray nonce;
    QByteArray wrapped;   // ciphertext (32) || tag (16)
};

struct Parsed {
    TecaLayer::Info  info;
    QByteArray       fixed;          // header bytes 0..39
    QList<Recipient> recipients;
    QByteArray       payloadNonce;
    QByteArray       header;         // every header byte, digest included
    QByteArray       ciphertext;     // points into the container (fromRawData)
    QByteArray       tag;
};

TecaLayer::Result parse(const QByteArray &container, Parsed *out, QString *error)
{
    using R = TecaLayer::Result;
    if (container.size() < qsizetype(sizeof kMagic)
        || std::memcmp(container.constData(), kMagic, sizeof kMagic) != 0) {
        setError(error, QStringLiteral("not a Teca layer container"));
        return R::Malformed;
    }
    const QString cutShort = QStringLiteral("the layer container is cut short");
    Reader r{container};
    r.pos = sizeof kMagic;
    const quint16 version = r.u16();
    if (!r.ok) { setError(error, cutShort); return R::Malformed; }
    if (version != TecaLayer::kFormatVersion) {
        setError(error, QStringLiteral("layer format version %1 is not supported (this build reads version %2)")
                            .arg(version).arg(TecaLayer::kFormatVersion));
        return R::UnsupportedVersion;
    }
    const quint8 layer = r.u8();
    const quint8 kdfId = r.u8();
    if (!r.ok) { setError(error, cutShort); return R::Malformed; }
    if (layer != quint8(TecaLayer::Id::Faces) && layer != quint8(TecaLayer::Id::People)) {
        setError(error, QStringLiteral("unknown layer id %1").arg(layer));
        return R::Malformed;
    }
    if (kdfId != kKdfArgon2id) {
        setError(error, QStringLiteral("unknown key-derivation function %1").arg(kdfId));
        return R::UnsupportedVersion;
    }
    Parsed p;
    p.info.version          = version;
    p.info.layer            = TecaLayer::Id(layer);
    p.info.kdf.memoryKiB    = r.u32();
    p.info.kdf.iterations   = r.u32();
    p.info.kdf.lanes        = r.u32();
    p.info.salt             = r.take(TecaCrypto::kSaltSize);
    const quint16 count     = r.u16();
    if (!r.ok) { setError(error, cutShort); return R::Malformed; }
    if (count < 1 || count > TecaLayer::kMaxRecipients) {
        setError(error, QStringLiteral("recipient count %1 out of range").arg(count));
        return R::Malformed;
    }
    p.fixed = container.left(kFixedSize);
    p.recipients.reserve(count);
    for (int i = 0; i < count && r.ok; ++i) {
        Recipient rc;
        rc.fingerprint = r.take(TecaCrypto::kFingerprintSize);
        rc.ephemeral   = r.take(TecaCrypto::kKeySize);
        rc.nonce       = r.take(TecaCrypto::kNonceSize);
        rc.wrapped     = r.take(kWrappedSize);
        p.recipients.append(rc);
        p.info.fingerprints.append(rc.fingerprint);
    }
    p.payloadNonce = r.take(TecaCrypto::kNonceSize);
    const quint64 length = r.u64();
    const qsizetype digestAt = r.pos;
    const QByteArray digest = r.take(32);
    if (!r.ok) { setError(error, cutShort); return R::Malformed; }
    const quint64 rest = quint64(container.size() - r.pos);
    if (rest < quint64(TecaCrypto::kTagSize) || length != rest - quint64(TecaCrypto::kTagSize)) {
        setError(error, QStringLiteral("the layer container length does not match its header"));
        return R::Malformed;
    }
    if (TecaCrypto::sha256(container.left(digestAt)) != digest) {
        setError(error, QStringLiteral("the layer header is damaged (digest mismatch)"));
        return R::Corrupted;
    }
    QString why;
    if (!TecaCrypto::kdfParamsAcceptable(p.info.kdf, &why)) {
        setError(error, QStringLiteral("key-derivation parameters not accepted: %1").arg(why));
        return R::UnsupportedVersion;
    }
    p.info.payloadSize = length;
    p.header     = container.left(r.pos);
    p.ciphertext = QByteArray::fromRawData(container.constData() + r.pos, qsizetype(length));
    p.tag        = container.right(TecaCrypto::kTagSize);
    if (out)
        *out = p;
    return R::Ok;
}

// W = HKDF-SHA256(Z || PK, salt, "Teca layer wrap v1" || layer || E || R)
QByteArray wrapKey(const QByteArray &shared, const QByteArray &passwordKey, const QByteArray &salt,
                   TecaLayer::Id layer, const QByteArray &ephemeral, const QByteArray &reader,
                   QString *error)
{
    QByteArray ikm = shared + passwordKey;
    WipeOnExit wipeIkm{ikm};
    QByteArray info(kWrapLabel);
    info.append(char(layer));
    info.append(ephemeral);
    info.append(reader);
    return TecaCrypto::hkdfSha256(ikm, salt, info, TecaCrypto::kKeySize, error);
}

TecaLayer::Result sealInternal(TecaLayer::Id layer, const QByteArray &plaintext,
                               const QString &password, const QList<QByteArray> &recipients,
                               const TecaCrypto::KdfParams &kdf, QByteArray *out, QString *error)
{
    using R = TecaLayer::Result;
    if (layer != TecaLayer::Id::Faces && layer != TecaLayer::Id::People) {
        setError(error, QStringLiteral("unknown layer id"));
        return R::InvalidArgument;
    }
    if (password.isEmpty()) {
        setError(error, QStringLiteral("a layer password is needed"));
        return R::InvalidArgument;
    }
    if (recipients.isEmpty()) {
        setError(error, QStringLiteral("a layer needs at least one reader"));
        return R::InvalidArgument;
    }
    QString why;
    if (!TecaCrypto::kdfParamsAcceptable(kdf, &why)) {
        setError(error, why);
        return R::InvalidArgument;
    }
    QList<QByteArray> keys, fingerprints;
    for (const QByteArray &key : recipients) {
        if (key.size() != TecaCrypto::kKeySize) {
            setError(error, QStringLiteral("a reader public key must be 32 bytes"));
            return R::InvalidArgument;
        }
        const QByteArray fp = TecaCrypto::publicKeyFingerprint(key);
        if (!fingerprints.contains(fp)) {
            keys.append(key);
            fingerprints.append(fp);
        }
    }
    if (keys.size() > TecaLayer::kMaxRecipients) {
        setError(error, QStringLiteral("too many readers (at most %1)").arg(TecaLayer::kMaxRecipients));
        return R::InvalidArgument;
    }

    const QByteArray salt = TecaCrypto::randomBytes(TecaCrypto::kSaltSize, error);
    if (salt.isEmpty())
        return R::CryptoFailure;
    QByteArray passwordKey;
    {
        QByteArray pw = TecaCrypto::passwordBytes(password);
        WipeOnExit wipePw{pw};
        passwordKey = TecaCrypto::argon2id(pw, salt, kdf, TecaCrypto::kKeySize, error);
    }
    WipeOnExit wipePk{passwordKey};
    if (passwordKey.isEmpty())
        return R::CryptoFailure;
    QByteArray dek = TecaCrypto::randomBytes(TecaCrypto::kKeySize, error);
    WipeOnExit wipeDek{dek};
    if (dek.isEmpty())
        return R::CryptoFailure;

    QByteArray header;
    header.reserve(kFixedSize + kRecipientSize * keys.size() + kTrailerSize);
    header.append(kMagic, sizeof kMagic);
    putU16(header, TecaLayer::kFormatVersion);
    putU8(header, quint8(layer));
    putU8(header, kKdfArgon2id);
    putU32(header, kdf.memoryKiB);
    putU32(header, kdf.iterations);
    putU32(header, kdf.lanes);
    header.append(salt);
    const QByteArray fixed = header;   // bytes 0..39
    putU16(header, quint16(keys.size()));

    for (int i = 0; i < keys.size(); ++i) {
        TecaCrypto::KeyPair ephemeral = TecaCrypto::generateX25519(error);
        WipeOnExit wipeEph{ephemeral.privateKey};
        if (!ephemeral.isValid())
            return R::CryptoFailure;
        QByteArray shared = TecaCrypto::x25519SharedSecret(ephemeral.privateKey, keys.at(i), &why);
        WipeOnExit wipeShared{shared};
        if (shared.isEmpty()) {
            setError(error, QStringLiteral("reader public key %1 is not usable: %2").arg(i + 1).arg(why));
            return R::InvalidArgument;
        }
        QByteArray wrap = wrapKey(shared, passwordKey, salt, layer, ephemeral.publicKey, keys.at(i), error);
        WipeOnExit wipeWrap{wrap};
        if (wrap.isEmpty())
            return R::CryptoFailure;
        const QByteArray nonce = TecaCrypto::randomBytes(TecaCrypto::kNonceSize, error);
        if (nonce.isEmpty())
            return R::CryptoFailure;
        QByteArray wrapped(kWrappedSize, Qt::Uninitialized);
        if (!gcmEncryptRaw(wrap, nonce, dek.constData(), dek.size(),
                           fixed + fingerprints.at(i) + ephemeral.publicKey,
                           wrapped.data(), wrapped.data() + TecaCrypto::kKeySize, error))
            return R::CryptoFailure;
        header.append(fingerprints.at(i));
        header.append(ephemeral.publicKey);
        header.append(nonce);
        header.append(wrapped);
    }

    const QByteArray payloadNonce = TecaCrypto::randomBytes(TecaCrypto::kNonceSize, error);
    if (payloadNonce.isEmpty())
        return R::CryptoFailure;
    header.append(payloadNonce);
    putU64(header, quint64(plaintext.size()));
    header.append(TecaCrypto::sha256(header));

    QByteArray container(header.size() + plaintext.size() + TecaCrypto::kTagSize, Qt::Uninitialized);
    std::memcpy(container.data(), header.constData(), size_t(header.size()));
    char *payload = container.data() + header.size();
    if (!gcmEncryptRaw(dek, payloadNonce, plaintext.constData(), plaintext.size(), header,
                       payload, payload + plaintext.size(), error))
        return R::CryptoFailure;
    if (out)
        *out = container;
    return R::Ok;
}

} // namespace

// ============================================================ TecaCrypto

QByteArray TecaCrypto::randomBytes(int count, QString *error)
{
    if (count <= 0) {
        setError(error, QStringLiteral("invalid random length"));
        return QByteArray();
    }
    QByteArray out(count, Qt::Uninitialized);
    if (RAND_priv_bytes(bytes(out.data()), count) != 1) {
        setError(error, failure("secure random bytes are not available"));
        return QByteArray();
    }
    return out;
}

void TecaCrypto::wipe(QByteArray &secret)
{
    // Only a buffer this array owns alone is written to (a shared or raw one
    // belongs to someone else)
    if (!secret.isEmpty() && secret.isDetached())
        OPENSSL_cleanse(secret.data(), size_t(secret.size()));
    secret.clear();
}

void TecaCrypto::wipe(QString &secret)
{
    if (!secret.isEmpty() && secret.isDetached())
        OPENSSL_cleanse(secret.data(), size_t(secret.size()) * sizeof(QChar));
    secret.clear();
}

QByteArray TecaCrypto::passwordBytes(const QString &password)
{
    QString normalized = password.normalized(QString::NormalizationForm_C);
    QByteArray out = normalized.toUtf8();
    wipe(normalized);
    return out;
}

bool TecaCrypto::kdfParamsAcceptable(const KdfParams &params, QString *error)
{
    if (params.lanes < 1 || params.lanes > kKdfMaxLanes) {
        setError(error, QStringLiteral("Argon2id lanes must be between 1 and %1").arg(kKdfMaxLanes));
        return false;
    }
    if (params.iterations < 1 || params.iterations > kKdfMaxIterations) {
        setError(error, QStringLiteral("Argon2id iterations must be between 1 and %1").arg(kKdfMaxIterations));
        return false;
    }
    if (params.memoryKiB < kKdfMinMemoryKiB || params.memoryKiB > kKdfMaxMemoryKiB
        || params.memoryKiB < 8 * params.lanes) {
        setError(error, QStringLiteral("Argon2id memory must be between %1 and %2 KiB")
                            .arg(kKdfMinMemoryKiB).arg(kKdfMaxMemoryKiB));
        return false;
    }
    return true;
}

bool TecaCrypto::isArgon2idAvailable(QString *error)
{
    KdfPtr kdf(EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr));
    if (!kdf) {
        ERR_clear_error();
        setError(error, QStringLiteral("Argon2id is not available in %1: OpenSSL 3.2 or later is needed")
                            .arg(QString::fromLatin1(OpenSSL_version(OPENSSL_VERSION))));
        return false;
    }
    return true;
}

QByteArray TecaCrypto::argon2id(const QByteArray &password, const QByteArray &salt,
                                const KdfParams &params, int length, QString *error)
{
    if (salt.size() < 8 || length < 4 || length > 1024) {
        setError(error, QStringLiteral("Argon2id needs a salt of at least 8 bytes and an output of 4 to 1024 bytes"));
        return QByteArray();
    }
    if (params.lanes < 1 || params.lanes > 0xFFFFFF || params.iterations < 1
        || params.memoryKiB < 8 * params.lanes) {
        setError(error, QStringLiteral("Argon2id parameters out of range"));
        return QByteArray();
    }
    KdfPtr kdf(EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr));
    if (!kdf) {
        isArgon2idAvailable(error);
        return QByteArray();
    }
    QByteArray out(length, Qt::Uninitialized);
    quint32 threads = argon2Threads(params.lanes);
    for (;;) {
        KdfCtxPtr ctx(EVP_KDF_CTX_new(kdf.get()));
        if (!ctx) {
            wipe(out);
            setError(error, failure("Argon2id context not available"));
            return QByteArray();
        }
        quint32 iterations = params.iterations;
        quint32 lanes      = params.lanes;
        quint32 memory     = params.memoryKiB;
        quint32 version    = 0x13;
        quint32 earlyClean = 1;
        // Parameter names spelled out: the OSSL_KDF_PARAM_ARGON2_* macros
        // only exist from OpenSSL 3.2 headers on
        OSSL_PARAM p[] = {
            OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
                                              const_cast<char *>(password.constData()),
                                              size_t(password.size())),
            OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                              const_cast<char *>(salt.constData()),
                                              size_t(salt.size())),
            OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iterations),
            OSSL_PARAM_construct_uint32("lanes", &lanes),
            OSSL_PARAM_construct_uint32("threads", &threads),
            OSSL_PARAM_construct_uint32("memcost", &memory),
            OSSL_PARAM_construct_uint32("version", &version),
            OSSL_PARAM_construct_uint32("early_clean", &earlyClean),
            OSSL_PARAM_construct_end()
        };
        if (EVP_KDF_derive(ctx.get(), bytes(out.data()), size_t(length), p) == 1)
            return out;
        if (threads > 1) {
            // The pool is busy with another derivation: one thread gives
            // the same key, only slower
            ERR_clear_error();
            threads = 1;
            continue;
        }
        wipe(out);
        setError(error, failure("Argon2id failed"));
        return QByteArray();
    }
}

QByteArray TecaCrypto::aesGcmSeal(const QByteArray &key, const QByteArray &plaintext,
                                  const QByteArray &aad, QString *error)
{
    if (key.size() != kKeySize) {
        setError(error, QStringLiteral("AES-256-GCM needs a 32-byte key"));
        return QByteArray();
    }
    QByteArray nonce(kNonceSize, Qt::Uninitialized);
    if (RAND_bytes(bytes(nonce.data()), kNonceSize) != 1) {
        setError(error, failure("secure random bytes are not available"));
        return QByteArray();
    }
    QByteArray sealed(kNonceSize + plaintext.size() + kTagSize, Qt::Uninitialized);
    std::memcpy(sealed.data(), nonce.constData(), kNonceSize);
    char *body = sealed.data() + kNonceSize;
    if (!gcmEncryptRaw(key, nonce, plaintext.constData(), plaintext.size(), aad,
                       body, body + plaintext.size(), error))
        return QByteArray();
    return sealed;
}

bool TecaCrypto::aesGcmOpen(const QByteArray &key, const QByteArray &sealed,
                            const QByteArray &aad, QByteArray *plaintext, QString *error)
{
    if (plaintext)
        plaintext->clear();
    if (sealed.size() < kNonceSize + kTagSize) {
        setError(error, QStringLiteral("AES-GCM data is too short"));
        return false;
    }
    const qsizetype bodySize = sealed.size() - kNonceSize - kTagSize;
    return aesGcmDecrypt(key, sealed.left(kNonceSize),
                         QByteArray::fromRawData(sealed.constData() + kNonceSize, bodySize),
                         sealed.right(kTagSize), aad, plaintext, error);
}

bool TecaCrypto::aesGcmEncrypt(const QByteArray &key, const QByteArray &nonce,
                               const QByteArray &plaintext, const QByteArray &aad,
                               QByteArray *ciphertext, QByteArray *tag, QString *error)
{
    if (!ciphertext || !tag) {
        setError(error, QStringLiteral("no output for AES-256-GCM"));
        return false;
    }
    QByteArray body(plaintext.size(), Qt::Uninitialized);
    QByteArray t(kTagSize, Qt::Uninitialized);
    if (!gcmEncryptRaw(key, nonce, plaintext.constData(), plaintext.size(), aad,
                       body.data(), t.data(), error))
        return false;
    *ciphertext = body;
    *tag = t;
    return true;
}

bool TecaCrypto::aesGcmDecrypt(const QByteArray &key, const QByteArray &nonce,
                               const QByteArray &ciphertext, const QByteArray &tag,
                               const QByteArray &aad, QByteArray *plaintext, QString *error)
{
    if (plaintext)
        plaintext->clear();
    if (key.size() != kKeySize || nonce.size() != kNonceSize || tag.size() != kTagSize) {
        setError(error, QStringLiteral("AES-256-GCM needs a 32-byte key, a 12-byte nonce and a 16-byte tag"));
        return false;
    }
    CipherPtr cipher(EVP_CIPHER_fetch(nullptr, "AES-256-GCM", nullptr));
    CipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!cipher || !ctx
        || EVP_DecryptInit_ex2(ctx.get(), cipher.get(), bytes(key), bytes(nonce), nullptr) != 1) {
        setError(error, failure("AES-256-GCM is not available"));
        return false;
    }
    int written = 0;
    for (qsizetype done = 0; done < aad.size(); done += kChunk) {
        const int piece = int(std::min(aad.size() - done, kChunk));
        if (EVP_DecryptUpdate(ctx.get(), nullptr, &written, bytes(aad) + done, piece) != 1) {
            setError(error, failure("AES-256-GCM failed on the associated data"));
            return false;
        }
    }
    QByteArray out(ciphertext.size(), Qt::Uninitialized);
    WipeOnExit wipeOut{out};
    qsizetype total = 0;
    for (qsizetype done = 0; done < ciphertext.size(); done += kChunk) {
        const int piece = int(std::min(ciphertext.size() - done, kChunk));
        written = 0;
        if (EVP_DecryptUpdate(ctx.get(), bytes(out.data()) + total, &written,
                              bytes(ciphertext) + done, piece) != 1) {
            setError(error, failure("AES-256-GCM decryption failed"));
            return false;
        }
        total += written;
    }
    OSSL_PARAM params[2] = {
        OSSL_PARAM_construct_octet_string(OSSL_CIPHER_PARAM_AEAD_TAG,
                                          const_cast<char *>(tag.constData()), size_t(kTagSize)),
        OSSL_PARAM_construct_end()
    };
    written = 0;
    if (EVP_CIPHER_CTX_set_params(ctx.get(), params) != 1
        || EVP_DecryptFinal_ex(ctx.get(), bytes(out.data()) + total, &written) != 1
        || total + written != ciphertext.size()) {
        ERR_clear_error();
        setError(error, QStringLiteral("AES-256-GCM authentication failed"));
        return false;
    }
    if (plaintext)
        plaintext->swap(out);   // out (now empty or the old value) is wiped on exit
    return true;
}

namespace {

bool rawKeys(EVP_PKEY *key, QByteArray *privateKey, QByteArray *publicKey, QString *error)
{
    size_t length = TecaCrypto::kKeySize;
    if (privateKey) {
        QByteArray priv(TecaCrypto::kKeySize, Qt::Uninitialized);
        if (EVP_PKEY_get_raw_private_key(key, bytes(priv.data()), &length) != 1
            || length != size_t(TecaCrypto::kKeySize)) {
            TecaCrypto::wipe(priv);
            setError(error, failure("X25519 private key not readable"));
            return false;
        }
        *privateKey = priv;
    }
    if (publicKey) {
        QByteArray pub(TecaCrypto::kKeySize, Qt::Uninitialized);
        length = TecaCrypto::kKeySize;
        if (EVP_PKEY_get_raw_public_key(key, bytes(pub.data()), &length) != 1
            || length != size_t(TecaCrypto::kKeySize)) {
            setError(error, failure("X25519 public key not readable"));
            return false;
        }
        *publicKey = pub;
    }
    return true;
}

PkeyPtr privateKeyFromRaw(const QByteArray &privateKey, QString *error)
{
    if (privateKey.size() != TecaCrypto::kKeySize) {
        setError(error, QStringLiteral("an X25519 private key must be 32 bytes"));
        return PkeyPtr();
    }
    PkeyPtr key(EVP_PKEY_new_raw_private_key_ex(nullptr, "X25519", nullptr,
                                                bytes(privateKey), size_t(privateKey.size())));
    if (!key)
        setError(error, failure("X25519 private key not usable"));
    return key;
}

} // namespace

TecaCrypto::KeyPair TecaCrypto::generateX25519(QString *error)
{
    KeyPair pair;
    PkeyCtxPtr ctx(EVP_PKEY_CTX_new_from_name(nullptr, "X25519", nullptr));
    EVP_PKEY *generated = nullptr;
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1 || EVP_PKEY_generate(ctx.get(), &generated) != 1) {
        setError(error, failure("X25519 key generation failed"));
        return pair;
    }
    PkeyPtr key(generated);
    if (!rawKeys(key.get(), &pair.privateKey, &pair.publicKey, error)) {
        wipe(pair.privateKey);
        pair.publicKey.clear();
    }
    return pair;
}

QByteArray TecaCrypto::x25519PublicKey(const QByteArray &privateKey, QString *error)
{
    PkeyPtr key = privateKeyFromRaw(privateKey, error);
    QByteArray pub;
    if (key)
        rawKeys(key.get(), nullptr, &pub, error);
    return pub;
}

QByteArray TecaCrypto::x25519SharedSecret(const QByteArray &privateKey,
                                          const QByteArray &peerPublicKey, QString *error)
{
    if (peerPublicKey.size() != kKeySize) {
        setError(error, QStringLiteral("an X25519 public key must be 32 bytes"));
        return QByteArray();
    }
    PkeyPtr mine = privateKeyFromRaw(privateKey, error);
    if (!mine)
        return QByteArray();
    PkeyPtr theirs(EVP_PKEY_new_raw_public_key_ex(nullptr, "X25519", nullptr,
                                                  bytes(peerPublicKey), size_t(peerPublicKey.size())));
    PkeyCtxPtr ctx(theirs ? EVP_PKEY_CTX_new_from_pkey(nullptr, mine.get(), nullptr) : nullptr);
    QByteArray shared(kKeySize, Qt::Uninitialized);
    size_t length = size_t(kKeySize);
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1
        || EVP_PKEY_derive_set_peer(ctx.get(), theirs.get()) != 1
        || EVP_PKEY_derive(ctx.get(), bytes(shared.data()), &length) != 1
        || length != size_t(kKeySize)) {
        wipe(shared);
        setError(error, failure("X25519 key agreement failed (low-order or invalid public key)"));
        return QByteArray();
    }
    static const unsigned char zeros[kKeySize] = {};
    if (CRYPTO_memcmp(shared.constData(), zeros, size_t(kKeySize)) == 0) {
        wipe(shared);
        setError(error, QStringLiteral("X25519 key agreement gave a zero secret (low-order public key)"));
        return QByteArray();
    }
    return shared;
}

QByteArray TecaCrypto::hkdfSha256(const QByteArray &ikm, const QByteArray &salt,
                                  const QByteArray &info, int length, QString *error)
{
    if (ikm.isEmpty() || length < 1 || length > 255 * 32) {
        setError(error, QStringLiteral("HKDF needs input key material and an output of 1 to 8160 bytes"));
        return QByteArray();
    }
    KdfPtr kdf(EVP_KDF_fetch(nullptr, "HKDF", nullptr));
    KdfCtxPtr ctx(kdf ? EVP_KDF_CTX_new(kdf.get()) : nullptr);
    if (!ctx) {
        setError(error, failure("HKDF is not available"));
        return QByteArray();
    }
    char digest[] = "SHA256";
    OSSL_PARAM params[5];
    int n = 0;
    params[n++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0);
    params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                                    const_cast<char *>(ikm.constData()), size_t(ikm.size()));
    if (!salt.isEmpty())
        params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                                        const_cast<char *>(salt.constData()), size_t(salt.size()));
    if (!info.isEmpty())
        params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                                        const_cast<char *>(info.constData()), size_t(info.size()));
    params[n] = OSSL_PARAM_construct_end();
    QByteArray out(length, Qt::Uninitialized);
    if (EVP_KDF_derive(ctx.get(), bytes(out.data()), size_t(length), params) != 1) {
        wipe(out);
        setError(error, failure("HKDF failed"));
        return QByteArray();
    }
    return out;
}

QByteArray TecaCrypto::sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray TecaCrypto::publicKeyFingerprint(const QByteArray &publicKey)
{
    if (publicKey.size() != kKeySize)
        return QByteArray();
    return sha256(QByteArray(kFingerprintLabel) + publicKey);
}

QString TecaCrypto::shortFingerprint(const QByteArray &publicKey)
{
    const QByteArray fp = publicKeyFingerprint(publicKey);
    if (fp.isEmpty())
        return QString();
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";   // Crockford
    QString out;
    quint32 buffer = 0;
    int bits = 0, produced = 0;
    for (int i = 0; i < 10; ++i) {   // 80 bits -> 16 characters
        buffer = (buffer << 8) | quint8(fp.at(i));
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.append(QLatin1Char(alphabet[(buffer >> bits) & 31]));
            if (++produced % 4 == 0 && produced < 16)
                out.append(QLatin1Char('-'));
        }
    }
    return out;
}

// ============================================================ TecaLayer

QByteArray TecaLayer::seal(Id layer, const QByteArray &plaintext, const QString &password,
                           const QList<QByteArray> &recipientPublicKeys, QString *error,
                           const TecaCrypto::KdfParams &kdf)
{
    QByteArray container;
    if (sealInternal(layer, plaintext, password, recipientPublicKeys, kdf, &container, error) != Result::Ok)
        return QByteArray();
    if (error)
        error->clear();
    return container;
}

QByteArray TecaLayer::open(const QByteArray &container, const QString &password,
                           const QByteArray &readerPrivateKey, Result *result,
                           QString *error, Info *info)
{
    auto fail = [&](Result code, const QString &text) {
        if (result)
            *result = code;
        setError(error, text);
        return QByteArray();
    };
    if (readerPrivateKey.size() != TecaCrypto::kKeySize)
        return fail(Result::InvalidArgument, QStringLiteral("the reader private key must be 32 bytes"));
    if (password.isEmpty())
        return fail(Result::InvalidArgument, QStringLiteral("a layer password is needed"));

    QString why;
    Parsed p;
    const Result parsed = parse(container, &p, &why);
    if (parsed != Result::Ok)
        return fail(parsed, why);

    const QByteArray readerPublic = TecaCrypto::x25519PublicKey(readerPrivateKey, &why);
    if (readerPublic.isEmpty())
        return fail(Result::InvalidArgument, why);
    const QByteArray fingerprint = TecaCrypto::publicKeyFingerprint(readerPublic);
    const Recipient *entry = nullptr;
    for (const Recipient &rc : std::as_const(p.recipients)) {
        if (rc.fingerprint == fingerprint) {
            entry = &rc;
            break;
        }
    }
    if (!entry)
        return fail(Result::NoAccess, QStringLiteral("this reader has no access to this layer"));

    QByteArray passwordKey;
    {
        QByteArray pw = TecaCrypto::passwordBytes(password);
        WipeOnExit wipePw{pw};
        passwordKey = TecaCrypto::argon2id(pw, p.info.salt, p.info.kdf, TecaCrypto::kKeySize, &why);
    }
    WipeOnExit wipePk{passwordKey};
    if (passwordKey.isEmpty())
        return fail(Result::CryptoFailure, why);

    QByteArray shared = TecaCrypto::x25519SharedSecret(readerPrivateKey, entry->ephemeral, &why);
    WipeOnExit wipeShared{shared};
    if (shared.isEmpty())   // a low-order ephemeral key only comes from an edited entry
        return fail(Result::Corrupted, QStringLiteral("the access entry of this reader is damaged"));
    QByteArray wrap = wrapKey(shared, passwordKey, p.info.salt, p.info.layer,
                              entry->ephemeral, readerPublic, &why);
    WipeOnExit wipeWrap{wrap};
    if (wrap.isEmpty())
        return fail(Result::CryptoFailure, why);

    QByteArray dek;
    WipeOnExit wipeDek{dek};
    if (!TecaCrypto::aesGcmDecrypt(wrap, entry->nonce, entry->wrapped.left(TecaCrypto::kKeySize),
                                   entry->wrapped.mid(TecaCrypto::kKeySize),
                                   p.fixed + entry->fingerprint + entry->ephemeral, &dek, nullptr)
        || dek.size() != TecaCrypto::kKeySize)
        return fail(Result::WrongPassword, QStringLiteral("wrong password for this layer"));

    QByteArray plaintext;
    if (!TecaCrypto::aesGcmDecrypt(dek, p.payloadNonce, p.ciphertext, p.tag, p.header,
                                   &plaintext, nullptr))
        return fail(Result::Corrupted, QStringLiteral("the layer data is damaged or was altered"));

    if (result)
        *result = Result::Ok;
    if (error)
        error->clear();
    if (info)
        *info = p.info;
    return plaintext;
}

QByteArray TecaLayer::rotate(const QByteArray &container, const QString &password,
                             const QByteArray &openerPrivateKey,
                             const QList<QByteArray> &newRecipientPublicKeys,
                             const QString &newPassword, Result *result, QString *error,
                             const TecaCrypto::KdfParams &kdf)
{
    Result code = Result::Ok;
    Info info;
    QByteArray plaintext = open(container, password, openerPrivateKey, &code, error, &info);
    WipeOnExit wipePlain{plaintext};
    if (code != Result::Ok) {
        if (result)
            *result = code;
        return QByteArray();
    }
    QByteArray rotated;
    code = sealInternal(info.layer, plaintext, newPassword.isEmpty() ? password : newPassword,
                        newRecipientPublicKeys, kdf, &rotated, error);
    if (result)
        *result = code;
    if (code != Result::Ok)
        return QByteArray();
    if (error)
        error->clear();
    return rotated;
}

TecaLayer::Result TecaLayer::inspect(const QByteArray &container, Info *info, QString *error)
{
    Parsed p;
    const Result code = parse(container, &p, error);
    if (code == Result::Ok && info)
        *info = p.info;
    return code;
}

bool TecaLayer::grantsAccess(const QByteArray &container, const QByteArray &readerPublicKey)
{
    Info info;
    if (inspect(container, &info) != Result::Ok)
        return false;
    const QByteArray fingerprint = TecaCrypto::publicKeyFingerprint(readerPublicKey);
    return !fingerprint.isEmpty() && info.fingerprints.contains(fingerprint);
}

QString TecaLayer::resultName(Result result)
{
    switch (result) {
    case Result::Ok:                 return QStringLiteral("ok");
    case Result::InvalidArgument:    return QStringLiteral("invalid argument");
    case Result::CryptoFailure:      return QStringLiteral("crypto failure");
    case Result::Malformed:          return QStringLiteral("malformed container");
    case Result::UnsupportedVersion: return QStringLiteral("unsupported version");
    case Result::Corrupted:          return QStringLiteral("corrupted");
    case Result::NoAccess:           return QStringLiteral("no access");
    case Result::WrongPassword:      return QStringLiteral("wrong password");
    }
    return QStringLiteral("unknown");
}
