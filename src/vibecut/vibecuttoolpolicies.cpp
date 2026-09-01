/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecuttools.h"

namespace {
VibeCutToolPolicy policy(const QString &name, VibeCutToolRisk risk, bool reversible = false, bool mutatesProject = false,
                         bool asynchronous = false, bool confirmationRequired = false)
{
    VibeCutToolPolicy result;
    result.name = name;
    result.risk = risk;
    result.reversible = reversible;
    result.mutatesProject = mutatesProject;
    result.asynchronous = asynchronous;
    result.confirmationRequired = confirmationRequired;
    return result;
}
} // namespace

QHash<QString, VibeCutToolPolicy> VibeCutTools::policies() const
{
    QHash<QString, VibeCutToolPolicy> result;
    const auto add = [&result](const VibeCutToolPolicy &entry) { result.insert(entry.name, entry); };

    add(policy(QStringLiteral("timeline_list_clips"), VibeCutToolRisk::ReadOnly));
    add(policy(QStringLiteral("timeline_get_selection"), VibeCutToolRisk::ReadOnly));
    add(policy(QStringLiteral("effect_apply"), VibeCutToolRisk::ReversibleEdit, true, true));
    add(policy(QStringLiteral("ask_user"), VibeCutToolRisk::ReadOnly));
    add(policy(QStringLiteral("speech_status"), VibeCutToolRisk::ReadOnly));
    add(policy(QStringLiteral("speech_setup"), VibeCutToolRisk::ExternalSideEffect, false, false, true));
    add(policy(QStringLiteral("generate_subtitles"), VibeCutToolRisk::MajorEdit, true, true, true));

    return result;
}
