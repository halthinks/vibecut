/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QString>

class VibeCutSecretStore
{
public:
    static bool available();
    static QString readSecret(const QString &key, QString *error = nullptr);
    static bool writeSecret(const QString &key, const QString &value, QString *error = nullptr);
    static bool removeSecret(const QString &key, QString *error = nullptr);

private:
    static QString folderName();
};
