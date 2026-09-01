/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutconversationcontext.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {
QJsonObject user(const QString &text)
{
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), text}};
}

QJsonObject assistantText(const QString &text)
{
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                       {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                         {QStringLiteral("text"), text}}}}};
}

QJsonObject assistantToolUse(const QString &id)
{
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                       {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                                                         {QStringLiteral("id"), id},
                                                                         {QStringLiteral("name"), QStringLiteral("timeline_list_clips")},
                                                                         {QStringLiteral("input"), QJsonObject{}}}}}};
}

QJsonObject toolResult(const QString &id)
{
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                       {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")},
                                                                         {QStringLiteral("tool_use_id"), id},
                                                                         {QStringLiteral("content"), QStringLiteral("{\"ok\":true}")}}}}}};
}
} // namespace

TEST_CASE("context compaction keeps complete recent tool exchanges", "[vibecut][context]")
{
    QJsonArray history;
    history.append(user(QStringLiteral("old question")));
    history.append(assistantText(QString(200, QLatin1Char('x'))));
    history.append(user(QStringLiteral("inspect the timeline")));
    history.append(assistantToolUse(QStringLiteral("tool-1")));
    history.append(toolResult(QStringLiteral("tool-1")));
    history.append(assistantText(QStringLiteral("found it")));

    VibeCutConversationContext::Limits limits;
    limits.maxMessages = 4;
    limits.maxBytes = 4096;
    const QJsonArray compacted = VibeCutConversationContext::compact(history, limits);

    REQUIRE(compacted.size() == 4);
    CHECK(compacted.first().toObject().value(QStringLiteral("content")).toString() == QStringLiteral("inspect the timeline"));
    CHECK(compacted.at(1).toObject().value(QStringLiteral("role")).toString() == QStringLiteral("assistant"));
    CHECK_FALSE(VibeCutConversationContext::isRegularUserMessage(compacted.at(2).toObject()));
}

TEST_CASE("context compaction never starts on a synthetic tool result", "[vibecut][context]")
{
    QJsonArray history;
    history.append(user(QStringLiteral("one")));
    history.append(assistantText(QStringLiteral("answer")));
    history.append(user(QStringLiteral("two")));
    history.append(assistantToolUse(QStringLiteral("tool-2")));
    history.append(toolResult(QStringLiteral("tool-2")));
    history.append(assistantText(QStringLiteral("done")));

    VibeCutConversationContext::Limits limits;
    limits.maxMessages = 3;
    limits.maxBytes = 4096;
    const QJsonArray compacted = VibeCutConversationContext::compact(history, limits);

    REQUIRE_FALSE(compacted.isEmpty());
    CHECK(VibeCutConversationContext::isRegularUserMessage(compacted.first().toObject()));
}
