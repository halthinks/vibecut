/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutsecretstore.h"

#include <memory>

#ifdef VIBECUT_HAVE_KWALLET
#include <KWallet>
#include <QApplication>
#include <QWidget>
#endif

QString VibeCutSecretStore::folderName()
{
    return QStringLiteral("VibeCut");
}

bool VibeCutSecretStore::available()
{
#ifdef VIBECUT_HAVE_KWALLET
    return true;
#else
    return false;
#endif
}

QString VibeCutSecretStore::readSecret(const QString &key, QString *error)
{
    if (error) error->clear();
    const QString cleanKey = key.trimmed();
    if (cleanKey.isEmpty()) {
        if (error) *error = QStringLiteral("Secret key must not be empty.");
        return QString();
    }
#ifdef VIBECUT_HAVE_KWALLET
    const WId windowId = QApplication::activeWindow() ? QApplication::activeWindow()->winId() : WId(0);
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), windowId, KWallet::Wallet::Synchronous));
    if (!wallet) {
        if (error) *error = QStringLiteral("KWallet could not be opened.");
        return QString();
    }
    if (!wallet->hasFolder(folderName()) && !wallet->createFolder(folderName())) {
        if (error) *error = QStringLiteral("KWallet folder could not be created.");
        return QString();
    }
    if (!wallet->setFolder(folderName())) {
        if (error) *error = QStringLiteral("KWallet folder could not be selected.");
        return QString();
    }
    QString value;
    if (wallet->readPassword(cleanKey, value) != 0) {
        if (error) *error = QStringLiteral("Secret was not found in KWallet.");
        return QString();
    }
    return value.trimmed();
#else
    if (error) *error = QStringLiteral("KWallet support is not available in this build.");
    return QString();
#endif
}

bool VibeCutSecretStore::writeSecret(const QString &key, const QString &value, QString *error)
{
    if (error) error->clear();
    const QString cleanKey = key.trimmed();
    if (cleanKey.isEmpty()) {
        if (error) *error = QStringLiteral("Secret key must not be empty.");
        return false;
    }
#ifdef VIBECUT_HAVE_KWALLET
    const WId windowId = QApplication::activeWindow() ? QApplication::activeWindow()->winId() : WId(0);
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), windowId, KWallet::Wallet::Synchronous));
    if (!wallet) {
        if (error) *error = QStringLiteral("KWallet could not be opened.");
        return false;
    }
    if (!wallet->hasFolder(folderName()) && !wallet->createFolder(folderName())) {
        if (error) *error = QStringLiteral("KWallet folder could not be created.");
        return false;
    }
    if (!wallet->setFolder(folderName())) {
        if (error) *error = QStringLiteral("KWallet folder could not be selected.");
        return false;
    }
    if (wallet->writePassword(cleanKey, value) != 0) {
        if (error) *error = QStringLiteral("Secret could not be written to KWallet.");
        return false;
    }
    return true;
#else
    Q_UNUSED(value)
    if (error) *error = QStringLiteral("KWallet support is not available in this build.");
    return false;
#endif
}

bool VibeCutSecretStore::removeSecret(const QString &key, QString *error)
{
    if (error) error->clear();
    const QString cleanKey = key.trimmed();
    if (cleanKey.isEmpty()) {
        if (error) *error = QStringLiteral("Secret key must not be empty.");
        return false;
    }
#ifdef VIBECUT_HAVE_KWALLET
    const WId windowId = QApplication::activeWindow() ? QApplication::activeWindow()->winId() : WId(0);
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), windowId, KWallet::Wallet::Synchronous));
    if (!wallet) {
        if (error) *error = QStringLiteral("KWallet could not be opened.");
        return false;
    }
    if (!wallet->hasFolder(folderName())) return true;
    if (!wallet->setFolder(folderName())) {
        if (error) *error = QStringLiteral("KWallet folder could not be selected.");
        return false;
    }
    if (wallet->removeEntry(cleanKey) != 0) {
        if (error) *error = QStringLiteral("Secret could not be removed from KWallet.");
        return false;
    }
    return true;
#else
    if (error) *error = QStringLiteral("KWallet support is not available in this build.");
    return false;
#endif
}
