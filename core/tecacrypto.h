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
// File Name:   tecacrypto.h
// Purpose:     Encrypted face and people layers of a collection published
//              for read-only Macs (fork only, D44, owner's decision of
//              2026-10-31, SpecMediaIntelligence.md)
// Description: TecaCrypto: the primitives, on OpenSSL's libcrypto (EVP):
//              random bytes, Argon2id, AES-256-GCM, X25519, HKDF-SHA256,
//              SHA-256 fingerprints, wiping of secrets.
//              TecaLayer: the layer container below (seal, open, rotate).
//              Plain Qt value types and an English error string for the
//              caller (internal, not translated); no exceptions, no UI.
//              Decrypted data is only ever returned in memory: nothing here
//              writes a file.
/////////////////////////////////////////////////////////////////////////////

TECA LAYER CONTAINER, format version 1. Every integer is big-endian.

  Header
    offset  size  field
       0      8   magic "TECALAYR"
       8      2   format version = 1
      10      1   layer id: 'A' (0x41) faces, 'B' (0x42) people
      11      1   KDF id: 1 = Argon2id, version 0x13
      12      4   KDF memory, in KiB
      16      4   KDF iterations
      20      4   KDF lanes (parallelism; threads are a run-time choice and
                  do not change the result)
      24     16   KDF salt, random at every seal
      40      2   recipient count N, 1..kMaxRecipients
      42  N*124   recipients, in the order they were given, each:
                    32  fingerprint = SHA-256("Teca reader key v1" || R)
                    32  E, ephemeral X25519 public key (new per recipient
                        and per seal)
                    12  wrap nonce
                    48  wrapped DEK: AES-256-GCM ciphertext (32) || tag (16)
       .     12   payload nonce
       .      8   payload ciphertext length L
       .     32   header digest = SHA-256 of every header byte above
  Payload
             L    AES-256-GCM ciphertext of the layer data under the DEK
            16    GCM tag
  The container is exactly 42 + 124*N + 52 + L + 16 bytes long.

  Keys (R = reader public key, r = reader private key, e/E = ephemeral pair)
    PK   = Argon2id(password as NFC UTF-8, salt, memory, iterations, lanes),
           32 bytes
    Z    = X25519(e, R) = X25519(r, E)
    W    = HKDF-SHA256(IKM = Z || PK, salt = KDF salt,
                       info = "Teca layer wrap v1" || layer id || E || R),
           32 bytes
    wrapped DEK = AES-256-GCM(W, wrap nonce, DEK,
                              AAD = header bytes 0..39 || fingerprint || E)
    payload     = AES-256-GCM(DEK, payload nonce, layer data,
                              AAD = every header byte, digest included)
    DEK  = 32 random bytes, new at every seal.

  Double binding: the only copy of the DEK is wrapped under W, and W needs
  both Z (the reader's private key) and PK (the password). There is no
  wrap under the password alone nor under the reader key alone, and no
  password verifier: the container does not let anyone test passwords
  without a reader's private key.

  Telling the failures apart (TecaLayer::Result), in this order:
    Malformed / UnsupportedVersion  not a layer, cut short, unknown version
                                    or KDF parameters outside the bounds
    Corrupted   the header digest does not match (accidental damage), or
                the payload fails authentication once the DEK is unwrapped
    NoAccess    this Mac's fingerprint is not in the recipient list: known
                before the password is asked or derived
    WrongPassword  the fingerprint is there, but the DEK does not unwrap.
                The private key belongs to the entry (same fingerprint), so
                the X25519 half of W is right and the digest has ruled out
                accidental damage: what is left is the password. The digest
                is not keyed, so someone who edits an entry on purpose and
                recomputes it gets "wrong password" instead of "corrupted":
                the layer stays locked either way, only the reason differs.

  Rotation: every seal draws a new DEK, salt and ephemeral keys, so a seal
  without a reader is already the rotation that removes it. TecaLayer::rotate
  does the same from an existing container (it needs a reader that can open
  it). A removed reader keeps whatever older containers it already copied.
*/
#ifndef TECACRYPTO_H
#define TECACRYPTO_H

#include <QByteArray>
#include <QList>
#include <QString>

class TecaCrypto
{
public:
    static constexpr int kKeySize         = 32;   // AES-256, X25519 and derived keys
    static constexpr int kNonceSize       = 12;   // AES-GCM nonce (96 bits)
    static constexpr int kTagSize         = 16;   // AES-GCM tag
    static constexpr int kSaltSize        = 16;   // Argon2id salt
    static constexpr int kFingerprintSize = 32;   // SHA-256

    // ---- Random bytes and secrets
    // count bytes from OpenSSL's private DRBG; empty on failure
    static QByteArray randomBytes(int count, QString *error = nullptr);
    // Zeroes the buffer (OPENSSL_cleanse) and clears it. A buffer shared
    // with another QByteArray is only cleared: the caller should hold the
    // only copy of a secret.
    static void wipe(QByteArray &secret);
    static void wipe(QString &secret);
    // A password as the KDF sees it: Unicode NFC, then UTF-8 (the same
    // password typed on two Macs gives the same bytes)
    static QByteArray passwordBytes(const QString &password);

    // ---- Argon2id (OpenSSL 3.2 or later at run time)
    // Stored with every container, so later seals may change them.
    // Default for a desktop: 256 MiB, 4 passes, 4 lanes (about 0.15 s on
    // an M5 Max with 4 threads, under a second on a single thread of an
    // older Mac).
    struct KdfParams {
        quint32 memoryKiB  = 256 * 1024;
        quint32 iterations = 4;
        quint32 lanes      = 4;
        bool operator==(const KdfParams &other) const
        {
            return memoryKiB == other.memoryKiB && iterations == other.iterations
                   && lanes == other.lanes;
        }
    };
    // Bounds a container's parameters must respect, on seal and on open (a
    // container cannot make a reader spend unbounded memory or time)
    static constexpr quint32 kKdfMinMemoryKiB   = 64 * 1024;          // 64 MiB
    static constexpr quint32 kKdfMaxMemoryKiB   = 2 * 1024 * 1024;    // 2 GiB
    static constexpr quint32 kKdfMaxIterations  = 64;
    static constexpr quint32 kKdfMaxLanes       = 64;
    static bool kdfParamsAcceptable(const KdfParams &params, QString *error = nullptr);
    // Whether this libcrypto has Argon2id (false before OpenSSL 3.2)
    static bool isArgon2idAvailable(QString *error = nullptr);
    // Raw Argon2id; only the algorithm's own limits apply here (salt of at
    // least 8 bytes, memory of at least 8 KiB per lane). Empty on failure.
    static QByteArray argon2id(const QByteArray &password, const QByteArray &salt,
                               const KdfParams &params, int length = kKeySize,
                               QString *error = nullptr);

    // ---- AES-256-GCM
    // Random 96-bit nonce; returns nonce || ciphertext || tag, empty on failure
    static QByteArray aesGcmSeal(const QByteArray &key, const QByteArray &plaintext,
                                 const QByteArray &aad, QString *error = nullptr);
    // Opens what aesGcmSeal returned; false when the key, the AAD or the
    // data do not authenticate (plaintext is then empty)
    static bool aesGcmOpen(const QByteArray &key, const QByteArray &sealed,
                           const QByteArray &aad, QByteArray *plaintext,
                           QString *error = nullptr);
    // The same with a nonce chosen by the caller (each nonce once per key)
    static bool aesGcmEncrypt(const QByteArray &key, const QByteArray &nonce,
                              const QByteArray &plaintext, const QByteArray &aad,
                              QByteArray *ciphertext, QByteArray *tag,
                              QString *error = nullptr);
    static bool aesGcmDecrypt(const QByteArray &key, const QByteArray &nonce,
                              const QByteArray &ciphertext, const QByteArray &tag,
                              const QByteArray &aad, QByteArray *plaintext,
                              QString *error = nullptr);

    // ---- X25519 and HKDF (raw 32-byte keys, RFC 7748 / RFC 5869)
    struct KeyPair {
        QByteArray privateKey;
        QByteArray publicKey;
        bool isValid() const { return privateKey.size() == kKeySize && publicKey.size() == kKeySize; }
    };
    static KeyPair    generateX25519(QString *error = nullptr);
    static QByteArray x25519PublicKey(const QByteArray &privateKey, QString *error = nullptr);
    // Shared secret; fails on a low-order peer key (all-zero result)
    static QByteArray x25519SharedSecret(const QByteArray &privateKey,
                                         const QByteArray &peerPublicKey,
                                         QString *error = nullptr);
    static QByteArray hkdfSha256(const QByteArray &ikm, const QByteArray &salt,
                                 const QByteArray &info, int length = kKeySize,
                                 QString *error = nullptr);
    static QByteArray sha256(const QByteArray &data);

    // ---- Reader key fingerprints
    // SHA-256("Teca reader key v1" || public key): the id of a reader in a
    // container's recipient list
    static QByteArray publicKeyFingerprint(const QByteArray &publicKey);
    // Short form for people to compare: the first 80 bits of the
    // fingerprint in Crockford base32, "XXXX-XXXX-XXXX-XXXX" (no I, L, O, U)
    static QString shortFingerprint(const QByteArray &publicKey);
};

class TecaLayer
{
public:
    static constexpr quint16 kFormatVersion = 1;
    static constexpr int     kMaxRecipients = 1024;

    enum class Id : quint8 {
        Faces  = 'A',   // layer A: face boxes, crops, face rows, face -> person links
        People = 'B'    // layer B: names, roles, organisations, person <-> file links
    };

    enum class Result {
        Ok,
        InvalidArgument,     // bad call: empty password, key of the wrong size...
        CryptoFailure,       // libcrypto failed (Argon2id missing, out of memory...)
        Malformed,           // not a layer container, or cut short
        UnsupportedVersion,  // newer format or KDF than this build knows
        Corrupted,           // damaged header or payload
        NoAccess,            // this Mac is not among the recipients
        WrongPassword        // this Mac is a recipient, the password is wrong
    };

    struct Info {
        quint16                 version = 0;
        Id                      layer   = Id::Faces;
        TecaCrypto::KdfParams   kdf;
        QByteArray              salt;
        QList<QByteArray>       fingerprints;   // TecaCrypto::publicKeyFingerprint of each reader
        quint64                 payloadSize = 0;
    };

    // Encrypts data for the readers whose X25519 public keys are given (at
    // least one; duplicates are kept once). Empty on failure.
    static QByteArray seal(Id layer, const QByteArray &plaintext, const QString &password,
                           const QList<QByteArray> &recipientPublicKeys,
                           QString *error = nullptr,
                           const TecaCrypto::KdfParams &kdf = TecaCrypto::KdfParams());

    // The layer data, with the reader's private key and the layer password.
    // result tells Ok from the failures above (Ok with empty data is an
    // empty layer); info, when given, receives the header on Ok.
    static QByteArray open(const QByteArray &container, const QString &password,
                           const QByteArray &readerPrivateKey,
                           Result *result = nullptr, QString *error = nullptr,
                           Info *info = nullptr);

    // Opens the container, then seals its data again for newRecipients with
    // a new DEK, salt and ephemeral keys (and newPassword when not empty,
    // else the same password). Readers left out cannot open the result.
    static QByteArray rotate(const QByteArray &container, const QString &password,
                             const QByteArray &openerPrivateKey,
                             const QList<QByteArray> &newRecipientPublicKeys,
                             const QString &newPassword = QString(),
                             Result *result = nullptr, QString *error = nullptr,
                             const TecaCrypto::KdfParams &kdf = TecaCrypto::KdfParams());

    // Reads and checks the header (digest included) without any secret
    static Result inspect(const QByteArray &container, Info *info, QString *error = nullptr);

    // Whether the reader with this public key is a recipient (no secret
    // needed: lets the caller report "no access" before asking a password)
    static bool grantsAccess(const QByteArray &container, const QByteArray &readerPublicKey);

    // Internal English name of a result, for logs
    static QString resultName(Result result);
};

#endif // TECACRYPTO_H
