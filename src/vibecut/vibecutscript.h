/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QJsonObject>
#include <QString>

class VibeCutPlanRuntime;
class VibeCutToolSurface;

class VibeCutScriptSandbox
{
public:
    struct Result {
        bool ok = false;
        bool timedOut = false;
        QJsonObject value;
        QString error;
    };

    static Result evaluatePlan(const QString &source, int timeoutMs = 250);
};

bool registerVibeCutScriptTools(VibeCutToolSurface &surface, VibeCutPlanRuntime *runtime, QString *error = nullptr);
