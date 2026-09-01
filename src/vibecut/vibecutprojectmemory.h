/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QJsonArray>
#include <QString>
#include <QUrl>

class VibeCutProjectMemory
{
public:
    static constexpr int MaxEntries = 100;
    static constexpr qint64 MaxBytes = 65536;

    static QString fileName();
    static QJsonArray loadForProjectUrl(const QUrl &projectUrl, QString *error = nullptr);
    static QJsonArray loadCurrent(QString *error = nullptr);
    static QString contextText(QString *error = nullptr);
    static bool putCurrent(const QString &text, const QString &source, QString *id = nullptr, QString *error = nullptr);
    static bool forgetCurrent(const QString &id, QString *error = nullptr);
};
