/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutpolicyoverrides.h"

#include <QJsonArray>

TEST_CASE("project auto-allow cannot waive code-defined hard confirmation", "[vibecut][policy][overrides]")
{
    VibeCutToolPolicy hard;
    hard.name = QStringLiteral("hard_confirm");
    hard.risk = VibeCutToolRisk::ExternalSideEffect;
    hard.confirmationRequired = true;

    VibeCutToolPolicy ordinary;
    ordinary.name = QStringLiteral("ordinary_edit");
    ordinary.risk = VibeCutToolRisk::ReversibleEdit;

    QHash<QString, VibeCutToolPolicy> base;
    base.insert(hard.name, hard);
    base.insert(ordinary.name, ordinary);

    const QJsonObject config{{QStringLiteral("auto_allow"),
                              QJsonArray{QStringLiteral("hard_confirm"), QStringLiteral("ordinary_edit")}}};
    const auto effective = VibeCutPolicyOverrides::applyObject(base, config);

    CHECK(effective.value(QStringLiteral("hard_confirm")).confirmationRequired);
    CHECK_FALSE(effective.value(QStringLiteral("hard_confirm")).autoAllowed);
    CHECK(effective.value(QStringLiteral("hard_confirm")).requiresConfirmation(VibeCutTrustMode::Turbo));

    CHECK_FALSE(effective.value(QStringLiteral("ordinary_edit")).confirmationRequired);
    CHECK(effective.value(QStringLiteral("ordinary_edit")).autoAllowed);
}

TEST_CASE("project always-confirm can strengthen auto-allow and wins conflicts", "[vibecut][policy][overrides]")
{
    VibeCutToolPolicy edit;
    edit.name = QStringLiteral("edit");
    edit.risk = VibeCutToolRisk::ReversibleEdit;
    QHash<QString, VibeCutToolPolicy> base{{edit.name, edit}};

    const QJsonObject config{{QStringLiteral("auto_allow"), QJsonArray{edit.name}},
                             {QStringLiteral("always_confirm"), QJsonArray{edit.name}}};
    const auto effective = VibeCutPolicyOverrides::applyObject(base, config);
    CHECK(effective.value(edit.name).confirmationRequired);
    CHECK_FALSE(effective.value(edit.name).autoAllowed);
    CHECK(effective.value(edit.name).requiresConfirmation(VibeCutTrustMode::Turbo));
}
