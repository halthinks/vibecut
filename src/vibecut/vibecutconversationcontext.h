/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QJsonArray>

class VibeCutConversationContext
{
public:
    struct Limits {
        int maxMessages = 40;
        int maxBytes = 192 * 1024;
    };

    static QJsonArray compact(const QJsonArray &messages, const Limits &limits = Limits());
    static int approximateBytes(const QJsonArray &messages);
    static bool isRegularUserMessage(const QJsonObject &message);
};
