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
// File Name:   mackeychain.h
// Purpose:     This Mac's reader key for the encrypted face and people
//              layers (fork only, D44, owner's decision of 2026-10-31)
// Description: Each reader Mac makes one X25519 key pair (TecaCrypto) and
//              keeps it in the macOS keychain; the owner grants access to
//              its public key, and the next publish wraps the layer keys
//              for it (TecaLayer).
//
//              Where the key is: the LOGIN keychain (file-based), as a
//              generic password item: service "Teca reader key", account
//              "reader-x25519-v1", data = the 32-byte private key, generic
//              attribute = the 32-byte public key (readable without the
//              secret, so the access-request code needs no prompt).
//              Teca is signed ad hoc: the data-protection keychain, and with
//              it access control enforced by the keychain itself
//              (kSecAttrAccessControl + user presence), needs a
//              keychain-access-groups entitlement that only a provisioned
//              Developer ID signature carries. So the item sits in the login
//              keychain, and readerPrivateKey() reads it only after a
//              LocalAuthentication prompt (LAPolicyDeviceOwnerAuthentication:
//              Touch ID or the Mac password). That prompt is enforced by
//              Teca, not by the keychain: a program the item's access list
//              trusts could read it without the prompt. The login keychain
//              still encrypts the item at rest and asks the user before any
//              other program reads it; after every new Teca build (new ad-hoc
//              signature) macOS asks once more, with "Always Allow".
//
//              Account: one key per macOS user of the Mac ("per install"),
//              not per collection. Access is granted to a Mac: one access
//              request covers every collection the owner publishes for it,
//              and removing the Mac from a collection's readers is enough to
//              lock it out of that collection at the next publish. A
//              per-collection key would multiply requests and keychain items
//              for one gain, unlinkable fingerprints across collections of
//              the same owner, that D44 does not ask for. The "-v1" leaves
//              room for another algorithm later; the API takes any
//              service/account, so a per-collection key stays possible.
//
//              Other systems: a stub; every call reports "not available on
//              this system".
//              Blocking calls: readerPrivateKey() waits for the prompt;
//              call it from a worker thread, not the GUI thread.
//              Plain Qt value types, English error strings (internal).
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MACKEYCHAIN_H
#define MACKEYCHAIN_H

#include <QByteArray>
#include <QString>

// Which keychain item holds a reader key (tests use their own service)
struct MacReaderKeyRef {
    QString service = QStringLiteral("Teca reader key");
    QString account = QStringLiteral("reader-x25519-v1");
};

class MacKeychain
{
public:
    enum class Status {
        Ok,
        NotAvailable,      // not macOS
        NotFound,          // no reader key on this Mac yet
        Cancelled,         // the user dismissed the Touch ID / password prompt
        Denied,            // authentication failed, or the keychain refused
        InvalidArgument,
        Failed             // any other keychain or key error
    };

    // Tests cannot answer the prompt; SkipForTests reads the item without it
    // (and fails instead of showing any keychain dialog). Never used by the app.
    enum class Prompt { Required, SkipForTests };

    static bool isAvailable();

    // This Mac's reader public key, creating the key pair the first time
    // (no prompt: creating and reading the public half need none).
    // created tells whether this call made it.
    static QByteArray ensureReaderKey(const MacReaderKeyRef &ref = MacReaderKeyRef(),
                                      bool *created = nullptr, Status *status = nullptr,
                                      QString *error = nullptr);

    // The public key (32 bytes), or empty with NotFound when none exists
    static QByteArray readerPublicKey(const MacReaderKeyRef &ref = MacReaderKeyRef(),
                                      Status *status = nullptr, QString *error = nullptr);

    // TecaCrypto::shortFingerprint of the public key, for the access request
    static QString readerFingerprint(const MacReaderKeyRef &ref = MacReaderKeyRef(),
                                     Status *status = nullptr, QString *error = nullptr);

    // The private key (32 bytes), after Touch ID or the Mac password. reason
    // is the sentence the prompt shows (given and translated by the caller;
    // required). The caller wipes the result (TecaCrypto::wipe) after use.
    static QByteArray readerPrivateKey(const QString &reason,
                                       const MacReaderKeyRef &ref = MacReaderKeyRef(),
                                       Prompt prompt = Prompt::Required,
                                       Status *status = nullptr, QString *error = nullptr);

    // Removes the item (a new key pair then needs a new access grant).
    // true when it was removed or did not exist (status NotFound).
    static bool removeReaderKey(const MacReaderKeyRef &ref = MacReaderKeyRef(),
                                Status *status = nullptr, QString *error = nullptr);
};

#endif // MACKEYCHAIN_H
