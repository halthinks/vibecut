/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutplangate.h"

namespace {
VibeCutEditPlan samplePlan()
{
    VibeCutEditPlan plan;
    plan.id = QStringLiteral("plan");
    plan.baseRevision = 9;
    plan.objective = QStringLiteral("Clean then subtitle");

    VibeCutPlanOperation subtitles;
    subtitles.id = QStringLiteral("subtitles");
    subtitles.toolName = QStringLiteral("generate_subtitles");
    subtitles.dependsOn = {QStringLiteral("clean")};

    VibeCutPlanOperation clean;
    clean.id = QStringLiteral("clean");
    clean.toolName = QStringLiteral("effect_apply");

    plan.operations = {subtitles, clean};
    return plan;
}

QHash<QString, VibeCutToolPolicy> samplePolicies()
{
    QHash<QString, VibeCutToolPolicy> result;

    VibeCutToolPolicy effect;
    effect.name = QStringLiteral("effect_apply");
    effect.risk = VibeCutToolRisk::ReversibleEdit;
    effect.reversible = true;
    effect.mutatesProject = true;
    result.insert(effect.name, effect);

    VibeCutToolPolicy subtitles;
    subtitles.name = QStringLiteral("generate_subtitles");
    subtitles.risk = VibeCutToolRisk::MajorEdit;
    subtitles.reversible = true;
    subtitles.mutatesProject = true;
    result.insert(subtitles.name, subtitles);

    return result;
}
} // namespace

TEST_CASE("plan gate rejects stale state before execution", "[vibecut][plan]")
{
    const auto result = VibeCutPlanGate::assess(samplePlan(), 10, samplePolicies(), VibeCutTrustMode::Turbo, true);
    CHECK(result.status == VibeCutPlanGateStatus::StalePlan);
    CHECK_FALSE(result.ready());
}

TEST_CASE("plan gate requires approval for major edits in auto mode", "[vibecut][plan]")
{
    const auto blocked = VibeCutPlanGate::assess(samplePlan(), 9, samplePolicies(), VibeCutTrustMode::Auto, false);
    CHECK(blocked.status == VibeCutPlanGateStatus::ConfirmationRequired);

    const auto approved = VibeCutPlanGate::assess(samplePlan(), 9, samplePolicies(), VibeCutTrustMode::Auto, true);
    REQUIRE(approved.ready());
    REQUIRE(approved.executionOrder.size() == 2);
    CHECK(approved.executionOrder.at(0) == QStringLiteral("clean"));
    CHECK(approved.executionOrder.at(1) == QStringLiteral("subtitles"));
}

TEST_CASE("plan gate canonicalizes dependency-compatible sibling ordering", "[vibecut][plan][ordering]")
{
    VibeCutEditPlan plan;
    plan.id = QStringLiteral("canonical-order");
    plan.baseRevision = 9;
    plan.objective = QStringLiteral("Canonical sibling ordering");

    VibeCutPlanOperation b;
    b.id = QStringLiteral("b");
    b.toolName = QStringLiteral("effect_apply");
    b.dependsOn = {QStringLiteral("a")};

    VibeCutPlanOperation a;
    a.id = QStringLiteral("a");
    a.toolName = QStringLiteral("effect_apply");

    VibeCutPlanOperation c;
    c.id = QStringLiteral("c");
    c.toolName = QStringLiteral("effect_apply");
    c.dependsOn = {QStringLiteral("a")};

    plan.operations = {b, a, c};
    const auto result = VibeCutPlanGate::assess(plan, 9, samplePolicies(), VibeCutTrustMode::Turbo, true);
    REQUIRE(result.ready());
    CHECK(result.executionOrder == QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
}

TEST_CASE("plan gate fails closed on ungoverned tools", "[vibecut][plan]")
{
    VibeCutEditPlan plan = samplePlan();
    plan.operations.first().toolName = QStringLiteral("system.exec");

    const auto result = VibeCutPlanGate::assess(plan, 9, samplePolicies(), VibeCutTrustMode::Turbo, true);
    CHECK(result.status == VibeCutPlanGateStatus::UnknownTool);
    CHECK_FALSE(result.ready());
}

TEST_CASE("project-denied tools cannot execute even in turbo", "[vibecut][plan]")
{
    auto policies = samplePolicies();
    policies[QStringLiteral("effect_apply")].enabled = false;
    const auto result = VibeCutPlanGate::assess(samplePlan(), 9, policies, VibeCutTrustMode::Turbo, true);
    CHECK(result.status == VibeCutPlanGateStatus::ToolDenied);
    CHECK_FALSE(result.ready());
}

TEST_CASE("per-tool auto allow can waive global review except irreversible work", "[vibecut][plan]")
{
    VibeCutToolPolicy edit;
    edit.name = QStringLiteral("edit");
    edit.risk = VibeCutToolRisk::MajorEdit;
    CHECK(edit.requiresConfirmation(VibeCutTrustMode::Off));
    edit.autoAllowed = true;
    CHECK_FALSE(edit.requiresConfirmation(VibeCutTrustMode::Off));

    edit.risk = VibeCutToolRisk::Irreversible;
    CHECK(edit.requiresConfirmation(VibeCutTrustMode::Turbo));
}

TEST_CASE("always confirm overrides auto allow", "[vibecut][plan]")
{
    VibeCutToolPolicy edit;
    edit.name = QStringLiteral("edit");
    edit.risk = VibeCutToolRisk::ReversibleEdit;
    edit.autoAllowed = true;
    edit.confirmationRequired = true;
    CHECK(edit.requiresConfirmation(VibeCutTrustMode::Turbo));
}
