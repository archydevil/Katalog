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
// File Name:   mackeychain.mm
// Purpose:     This Mac's reader key in the login keychain, behind Touch ID
//              or the Mac password (fork only, D44). Objective-C++ with ARC;
//              built on macOS only (mackeychain_other.cpp elsewhere). Why
//              the login keychain and not the data-protection one: see
//              mackeychain.h.
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/platform/mackeychain.h"
#include "core/tecacrypto.h"

#include <QtGlobal>

#ifdef Q_OS_MACOS

#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#if !__has_feature(objc_arc)
#error "mackeychain.mm is built with -fobjc-arc (core/CMakeLists.txt)"
#endif

namespace {

using Status = MacKeychain::Status;

void report(Status *status, Status value, QString *error = nullptr, const QString &text = QString())
{
    if (status)
        *status = value;
    if (error)
        *error = text;
}

QString keychainText(OSStatus code)
{
    CFStringRef message = SecCopyErrorMessageString(code, nullptr);
    QString text = message ? QString::fromCFString(message) : QStringLiteral("keychain error");
    if (message)
        CFRelease(message);
    return QStringLiteral("%1 (OSStatus %2)").arg(text).arg(int(code));
}

Status statusFor(OSStatus code)
{
    switch (code) {
    case errSecSuccess:               return Status::Ok;
    case errSecItemNotFound:          return Status::NotFound;
    case errSecUserCanceled:          return Status::Cancelled;
    case errSecAuthFailed:
    case errSecInteractionNotAllowed: return Status::Denied;
    default:                          return Status::Failed;
    }
}

bool validRef(const MacReaderKeyRef &ref, Status *status, QString *error)
{
    if (ref.service.isEmpty() || ref.account.isEmpty()) {
        report(status, Status::InvalidArgument, error,
               QStringLiteral("a keychain service and account are needed"));
        return false;
    }
    return true;
}

// The item: generic password, service + account, in the file-based (login)
// keychain, never the data-protection one (see mackeychain.h)
NSMutableDictionary *itemQuery(const MacReaderKeyRef &ref)
{
    NSMutableDictionary *query = [NSMutableDictionary dictionary];
    query[(__bridge id)kSecClass]                     = (__bridge id)kSecClassGenericPassword;
    query[(__bridge id)kSecAttrService]               = ref.service.toNSString();
    query[(__bridge id)kSecAttrAccount]               = ref.account.toNSString();
    query[(__bridge id)kSecUseDataProtectionKeychain] = @NO;
    return query;
}

// Touch ID or the Mac password; blocks until the user answers
bool authenticate(const QString &reason, Status *status, QString *error)
{
    LAContext *context = [[LAContext alloc] init];
    NSError *canError = nil;
    if (![context canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication error:&canError]) {
        report(status, Status::Denied, error,
               QStringLiteral("Touch ID and the Mac password are not available: %1")
                   .arg(canError ? QString::fromNSString(canError.localizedDescription) : QString()));
        return false;
    }
    __block BOOL success = NO;
    __block NSInteger code = 0;
    __block NSString *description = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    // The reply comes on a private queue: waiting here does not deadlock,
    // but it blocks the calling thread for as long as the prompt is open
    [context evaluatePolicy:LAPolicyDeviceOwnerAuthentication
            localizedReason:reason.toNSString()
                      reply:^(BOOL ok, NSError *replyError) {
                          success = ok;
                          if (replyError) {
                              code = replyError.code;
                              description = [replyError.localizedDescription copy];
                          }
                          dispatch_semaphore_signal(done);
                      }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    [context invalidate];
    if (success)
        return true;
    const bool cancelled = code == LAErrorUserCancel || code == LAErrorSystemCancel
                           || code == LAErrorAppCancel || code == LAErrorUserFallback;
    report(status, cancelled ? Status::Cancelled : Status::Denied, error,
           QStringLiteral("authentication %1: %2")
               .arg(cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"),
                    description ? QString::fromNSString(description) : QString()));
    return false;
}

} // namespace

bool MacKeychain::isAvailable()
{
    return true;
}

QByteArray MacKeychain::readerPublicKey(const MacReaderKeyRef &ref, Status *status, QString *error)
{
    if (!validRef(ref, status, error))
        return QByteArray();
    @autoreleasepool {
        // Attributes only: the public key is not secret and reading it
        // needs neither the prompt nor the item's access list
        NSMutableDictionary *query = itemQuery(ref);
        query[(__bridge id)kSecReturnAttributes] = @YES;
        query[(__bridge id)kSecMatchLimit]       = (__bridge id)kSecMatchLimitOne;
        CFTypeRef result = nullptr;
        const OSStatus code = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
        NSDictionary *attributes = (__bridge_transfer NSDictionary *)result;
        if (code != errSecSuccess) {
            report(status, statusFor(code), error,
                   code == errSecItemNotFound ? QStringLiteral("no reader key on this Mac")
                                              : keychainText(code));
            return QByteArray();
        }
        id generic = attributes[(__bridge id)kSecAttrGeneric];
        const QByteArray publicKey = [generic isKindOfClass:[NSData class]]
                                         ? QByteArray::fromNSData((NSData *)generic)
                                         : QByteArray();
        if (publicKey.size() != TecaCrypto::kKeySize) {
            report(status, Status::Failed, error,
                   QStringLiteral("the reader key item has no valid public key"));
            return QByteArray();
        }
        report(status, Status::Ok, error);
        return publicKey;
    }
}

QByteArray MacKeychain::ensureReaderKey(const MacReaderKeyRef &ref, bool *created,
                                        Status *status, QString *error)
{
    if (created)
        *created = false;
    Status found = Status::Failed;
    QString why;
    const QByteArray existing = readerPublicKey(ref, &found, &why);
    if (found == Status::Ok) {
        report(status, Status::Ok, error);
        return existing;
    }
    if (found != Status::NotFound) {
        report(status, found, error, why);
        return QByteArray();
    }

    TecaCrypto::KeyPair pair = TecaCrypto::generateX25519(&why);
    if (!pair.isValid()) {
        report(status, Status::Failed, error, why);
        return QByteArray();
    }
    OSStatus code = errSecSuccess;
    @autoreleasepool {
        NSMutableDictionary *item = itemQuery(ref);
        item[(__bridge id)kSecAttrGeneric] = pair.publicKey.toNSData();
        // No copy of the secret on our side: the keychain copies the bytes
        item[(__bridge id)kSecValueData]   = pair.privateKey.toRawNSData();
        code = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
    }
    TecaCrypto::wipe(pair.privateKey);
    if (code == errSecDuplicateItem) {
        // Another process made it in the meantime: use that one
        return readerPublicKey(ref, status, error);
    }
    if (code != errSecSuccess) {
        report(status, statusFor(code), error, keychainText(code));
        return QByteArray();
    }
    if (created)
        *created = true;
    report(status, Status::Ok, error);
    return pair.publicKey;
}

QString MacKeychain::readerFingerprint(const MacReaderKeyRef &ref, Status *status, QString *error)
{
    const QByteArray publicKey = readerPublicKey(ref, status, error);
    return publicKey.isEmpty() ? QString() : TecaCrypto::shortFingerprint(publicKey);
}

QByteArray MacKeychain::readerPrivateKey(const QString &reason, const MacReaderKeyRef &ref,
                                         Prompt prompt, Status *status, QString *error)
{
    if (!validRef(ref, status, error))
        return QByteArray();
    if (prompt == Prompt::Required) {
        if (reason.trimmed().isEmpty()) {
            report(status, Status::InvalidArgument, error,
                   QStringLiteral("the authentication prompt needs a reason"));
            return QByteArray();
        }
        if (!authenticate(reason, status, error))
            return QByteArray();
    }
    QByteArray privateKey;
    OSStatus code = errSecSuccess;
    @autoreleasepool {
        NSMutableDictionary *query = itemQuery(ref);
        query[(__bridge id)kSecReturnData] = @YES;
        query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        if (prompt == Prompt::SkipForTests) {
            // A test cannot answer a keychain dialog either: fail instead
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            query[(__bridge id)kSecUseAuthenticationUI] = (__bridge id)kSecUseAuthenticationUIFail;
#pragma clang diagnostic pop
        }
        CFTypeRef result = nullptr;
        code = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
        NSData *data = (__bridge_transfer NSData *)result;
        if (code == errSecSuccess && [data isKindOfClass:[NSData class]])
            privateKey = QByteArray::fromNSData(data);
        // The keychain's own buffer is released with the pool; it cannot be
        // wiped from here (immutable NSData)
    }
    if (code != errSecSuccess) {
        report(status, statusFor(code), error,
               code == errSecItemNotFound ? QStringLiteral("no reader key on this Mac")
                                          : keychainText(code));
        return QByteArray();
    }
    if (privateKey.size() != TecaCrypto::kKeySize) {
        TecaCrypto::wipe(privateKey);
        report(status, Status::Failed, error, QStringLiteral("the reader key item is damaged"));
        return QByteArray();
    }
    report(status, Status::Ok, error);
    return privateKey;
}

bool MacKeychain::removeReaderKey(const MacReaderKeyRef &ref, Status *status, QString *error)
{
    if (!validRef(ref, status, error))
        return false;
    OSStatus code = errSecSuccess;
    @autoreleasepool {
        code = SecItemDelete((__bridge CFDictionaryRef)itemQuery(ref));
    }
    if (code == errSecSuccess || code == errSecItemNotFound) {
        report(status, code == errSecSuccess ? Status::Ok : Status::NotFound, error);
        return true;
    }
    report(status, statusFor(code), error, keychainText(code));
    return false;
}

#endif // Q_OS_MACOS
