/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QString>
#include <QUrl>

class VibeCutProjectRules
{
public:
    static QString fileName();
    static QString loadForProjectUrl(const QUrl &projectUrl, QString *error = nullptr);
    static QString loadCurrentProject(QString *error = nullptr);
    static QString appendToSystemPrompt(const QString &basePrompt, const QString &rules);

    enum { MaxRulesBytes = 64 * 1024 };
};
