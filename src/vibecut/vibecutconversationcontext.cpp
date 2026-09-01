/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutconversationcontext.h"

#include <QJsonDocument>
#include <QJsonObject>

int VibeCutConversationContext::approximateBytes(const QJsonArray &messages)
{
    return QJsonDocument(messages).toJson(QJsonDocument::Compact).size();
}

bool VibeCutConversationContext::isRegularUserMessage(const QJsonObject &message)
{
    if (message.value(QStringLiteral("role")).toString() != QLatin1String("user")) {
        return false;
    }

    const QJsonValue content = message.value(QStringLiteral("content"));
    if (content.isString()) {
        return true;
    }
    if (!content.isArray()) {
        return false;
    }

    const QJsonArray blocks = content.toArray();
    if (blocks.isEmpty()) {
        return false;
    }

    // Tool results are represented as synthetic user messages. They are not a
    // safe history boundary because dropping the assistant tool_use immediately
    // before them creates an invalid provider conversation.
    for (const QJsonValue &value : blocks) {
        if (value.toObject().value(QStringLiteral("type")).toString() == QLatin1String("tool_result")) {
            return false;
        }
    }
    return true;
}

QJsonArray VibeCutConversationContext::compact(const QJsonArray &messages)
{
    return compact(messages, Limits{});
}

QJsonArray VibeCutConversationContext::compact(const QJsonArray &messages, const Limits &limits)
{
    if (messages.size() <= limits.maxMessages && approximateBytes(messages) <= limits.maxBytes) {
        return messages;
    }

    // Only cut at an actual user-message boundary. Everything after that point
    // is kept intact, including assistant tool_use and synthetic user
    // tool_result pairs. Prefer the largest suffix that fits both budgets.
    for (int start = 0; start < messages.size(); ++start) {
        if (!isRegularUserMessage(messages.at(start).toObject())) {
            continue;
        }

        QJsonArray candidate;
        for (int i = start; i < messages.size(); ++i) {
            candidate.append(messages.at(i));
        }
        if (candidate.size() <= limits.maxMessages && approximateBytes(candidate) <= limits.maxBytes) {
            return candidate;
        }
    }

    // If the most recent complete exchange alone exceeds the configured byte
    // budget, correctness wins over an arbitrary cap: return that complete
    // exchange rather than splitting a tool protocol sequence.
    for (int start = messages.size() - 1; start >= 0; --start) {
        if (!isRegularUserMessage(messages.at(start).toObject())) {
            continue;
        }
        QJsonArray candidate;
        for (int i = start; i < messages.size(); ++i) {
            candidate.append(messages.at(i));
        }
        return candidate;
    }

    // Defensive fallback for malformed history. Returning the original array
    // is safer than inventing provider-invalid structure.
    return messages;
}
