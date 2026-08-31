/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutcontracts.h"

TEST_CASE("vibecut edit plans round-trip and reject stale revisions", "[vibecut][plan]")
{
    VibeCutEditPlan plan;
    plan.id = QStringLiteral("plan-1");
    plan.baseRevision = 42;
    plan.objective = QStringLiteral("Clean and subtitle the interview");

    VibeCutPlanOperation denoise;
    denoise.id = QStringLiteral("denoise");
    denoise.toolName = QStringLiteral("effect_apply");
    denoise.input = QJsonObject{{QStringLiteral("effect"), QStringLiteral("denoise")}};
    denoise.expectedPostconditions = {QStringLiteral("DeepFilterNet present on target clip")};
    plan.operations.append(denoise);

    VibeCutPlanOperation subtitles;
    subtitles.id = QStringLiteral("subtitles");
    subtitles.toolName = QStringLiteral("generate_subtitles");
    subtitles.dependsOn = {QStringLiteral("denoise")};
    plan.operations.append(subtitles);

    const VibeCutPlanValidation validation = plan.validate();
    CHECK(validation.ok);
    CHECK(plan.matchesRevision(42));
    CHECK_FALSE(plan.matchesRevision(43));

    const VibeCutEditPlan restored = VibeCutEditPlan::fromJson(plan.toJson());
    CHECK(restored.id == plan.id);
    CHECK(restored.baseRevision == plan.baseRevision);
    REQUIRE(restored.operations.size() == 2);
    CHECK(restored.operations.at(1).dependsOn == QStringList{QStringLiteral("denoise")});
}

TEST_CASE("vibecut edit plans reject broken dependency graphs", "[vibecut][plan]")
{
    VibeCutEditPlan plan;
    plan.id = QStringLiteral("broken");
    plan.objective = QStringLiteral("test");

    VibeCutPlanOperation first;
    first.id = QStringLiteral("a");
    first.toolName = QStringLiteral("tool_a");
    first.dependsOn = {QStringLiteral("b")};
    plan.operations.append(first);

    VibeCutPlanOperation second;
    second.id = QStringLiteral("b");
    second.toolName = QStringLiteral("tool_b");
    second.dependsOn = {QStringLiteral("a")};
    plan.operations.append(second);

    const VibeCutPlanValidation validation = plan.validate();
    CHECK_FALSE(validation.ok);
    CHECK(validation.errors.contains(QStringLiteral("operation dependency graph contains a cycle")));
}

TEST_CASE("vibecut trust policy keeps irreversible work confirm-only", "[vibecut][policy]")
{
    VibeCutToolPolicy read;
    read.name = QStringLiteral("timeline_list_clips");
    read.risk = VibeCutToolRisk::ReadOnly;

    VibeCutToolPolicy edit;
    edit.name = QStringLiteral("effect_apply");
    edit.risk = VibeCutToolRisk::ReversibleEdit;
    edit.reversible = true;
    edit.mutatesProject = true;

    VibeCutToolPolicy publish;
    publish.name = QStringLiteral("publish");
    publish.risk = VibeCutToolRisk::Irreversible;
    publish.confirmationRequired = true;

    CHECK_FALSE(read.requiresConfirmation(VibeCutTrustMode::Off));
    CHECK(edit.requiresConfirmation(VibeCutTrustMode::Off));
    CHECK_FALSE(edit.requiresConfirmation(VibeCutTrustMode::Auto));
    CHECK_FALSE(edit.requiresConfirmation(VibeCutTrustMode::Turbo));
    CHECK(publish.requiresConfirmation(VibeCutTrustMode::Auto));
    CHECK(publish.requiresConfirmation(VibeCutTrustMode::Turbo));
}
