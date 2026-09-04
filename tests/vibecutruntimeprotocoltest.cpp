/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"

#include "vibecut/vibecutruntimeprotocoladapter.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject request(const QString &type, const QJsonObject &payload, const QString &id = QStringLiteral("msg-1"))
{
    return QJsonObject{{QStringLiteral("v"), 1},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("kind"), QStringLiteral("request")},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("payload"), payload}};
}

QJsonObject errorPayload(const QJsonObject &envelope)
{
    REQUIRE(envelope.value(QStringLiteral("type")).toString() == QStringLiteral("error"));
    return envelope.value(QStringLiteral("payload")).toObject();
}

QJsonObject plan(quint64 revision, int operationCount = 2)
{
    QJsonArray operations;
    operations.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("op-1")},
                                  {QStringLiteral("tool"), QStringLiteral("protocol_fake_mutate")},
                                  {QStringLiteral("input"), QJsonObject{{QStringLiteral("value"), 1}}},
                                  {QStringLiteral("depends_on"), QJsonArray()},
                                  {QStringLiteral("expected_postconditions"), QJsonArray{QStringLiteral("value advanced")}}});
    if (operationCount > 1) {
        operations.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("op-2")},
                                      {QStringLiteral("tool"), QStringLiteral("protocol_fake_mutate")},
                                      {QStringLiteral("input"), QJsonObject{{QStringLiteral("value"), 2}}},
                                      {QStringLiteral("depends_on"), QJsonArray{QStringLiteral("op-1")}},
                                      {QStringLiteral("expected_postconditions"), QJsonArray{QStringLiteral("value advanced again")}}});
    }
    return QJsonObject{{QStringLiteral("id"), QStringLiteral("plan-1")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(revision)},
                       {QStringLiteral("objective"), QStringLiteral("Exercise protocol governance")},
                       {QStringLiteral("operations"), operations}};
}

struct Fixture {
    VibeCutTools base;
    VibeCutToolSurface surface{&base};
    quint64 revision = 7;
    int mutationCalls = 0;
    QJsonArray observedInputs;

    Fixture()
    {
        const QJsonObject mutateSchema{{QStringLiteral("name"), QStringLiteral("protocol_fake_mutate")},
                                       {QStringLiteral("description"), QStringLiteral("protocol fixture")},
                                       {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
        VibeCutToolPolicy mutatePolicy;
        mutatePolicy.name = QStringLiteral("protocol_fake_mutate");
        mutatePolicy.risk = VibeCutToolRisk::ReversibleEdit;
        mutatePolicy.reversible = true;
        mutatePolicy.mutatesProject = true;
        QString error;
        REQUIRE(surface.registerTool(mutateSchema, mutatePolicy, [this](const QJsonObject &input) {
            ++mutationCalls;
            observedInputs.append(input);
            ++revision;
            return QJsonObject{{QStringLiteral("ok"), true},
                               {QStringLiteral("observed_value"), input.value(QStringLiteral("value"))}};
        }, &error));
        REQUIRE(error.isEmpty());

        const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("protocol_fake_inspect")},
                                        {QStringLiteral("description"), QStringLiteral("protocol read fixture")},
                                        {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
        VibeCutToolPolicy inspectPolicy;
        inspectPolicy.name = QStringLiteral("protocol_fake_inspect");
        inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
        REQUIRE(surface.registerTool(inspectSchema, inspectPolicy, [this](const QJsonObject &) {
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("fixture_revision"), static_cast<qint64>(revision)}};
        }, &error));
        REQUIRE(error.isEmpty());
    }
};
} // namespace

TEST_CASE("protocol adapter exports live schema policy table and read-only inspection", "[vibecut][runtime-protocol]")
{
    Fixture fixture;
    VibeCutRuntimeProtocolAdapter adapter(&fixture.surface, [&fixture]() { return fixture.revision; });

    const QJsonObject hello = adapter.helloEnvelope(QStringLiteral("hello-1"), VibeCutTrustMode::Off);
    CHECK(hello.value(QStringLiteral("type")).toString() == QStringLiteral("hello"));
    const QJsonObject helloPayload = hello.value(QStringLiteral("payload")).toObject();
    CHECK(helloPayload.value(QStringLiteral("project_revision")).toVariant().toULongLong() == fixture.revision);
    bool foundMutate = false;
    for (const QJsonValue &value : helloPayload.value(QStringLiteral("tools")).toArray()) {
        const QJsonObject item = value.toObject();
        const QJsonObject schema = item.value(QStringLiteral("schema")).toObject();
        if (schema.value(QStringLiteral("name")).toString() != QLatin1String("protocol_fake_mutate")) continue;
        foundMutate = true;
        CHECK(item.value(QStringLiteral("policy")).toObject().value(QStringLiteral("risk")).toString() == QStringLiteral("reversible_edit"));
    }
    CHECK(foundMutate);

    const QJsonObject inspected = adapter.handleRequest(request(QStringLiteral("inspect"),
        QJsonObject{{QStringLiteral("operation"), QStringLiteral("protocol_fake_inspect")}, {QStringLiteral("input"), QJsonObject()}}));
    CHECK(inspected.value(QStringLiteral("type")).toString() == QStringLiteral("inspect"));
    CHECK(inspected.value(QStringLiteral("payload")).toObject().value(QStringLiteral("project_revision")).toVariant().toULongLong() == fixture.revision);
    CHECK(fixture.mutationCalls == 0);

    const QJsonObject refused = adapter.handleRequest(request(QStringLiteral("inspect"),
        QJsonObject{{QStringLiteral("operation"), QStringLiteral("protocol_fake_mutate")}, {QStringLiteral("input"), QJsonObject()}}));
    CHECK(errorPayload(refused).value(QStringLiteral("code")).toString() == QStringLiteral("inspect_not_read_only"));
    CHECK(fixture.mutationCalls == 0);
}

TEST_CASE("protocol review mode never mutates before explicit authorization", "[vibecut][runtime-protocol][authorization]")
{
    Fixture fixture;
    VibeCutRuntimeProtocolAdapter adapter(&fixture.surface, [&fixture]() { return fixture.revision; });
    const QJsonObject proposed = adapter.handleRequest(request(QStringLiteral("propose_plan"), plan(fixture.revision, 1)));
    CHECK(proposed.value(QStringLiteral("type")).toString() == QStringLiteral("propose_plan"));
    CHECK(proposed.value(QStringLiteral("payload")).toObject().value(QStringLiteral("requires_confirmation_off")).toBool(false));
    CHECK(fixture.mutationCalls == 0);

    const QJsonObject needsHuman = adapter.authorizePending(VibeCutTrustMode::Off, false, false);
    CHECK(errorPayload(needsHuman).value(QStringLiteral("code")).toString() == QStringLiteral("confirmation_required"));
    CHECK_FALSE(adapter.hasAuthorization());
    CHECK(fixture.mutationCalls == 0);

    const QJsonObject approved = adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    CHECK(approved.value(QStringLiteral("type")).toString() == QStringLiteral("authorize"));
    CHECK(approved.value(QStringLiteral("payload")).toObject().value(QStringLiteral("decision")).toString() == QStringLiteral("approved"));
    CHECK(adapter.hasAuthorization());
    CHECK(fixture.mutationCalls == 0);
}

TEST_CASE("protocol adapter rejects stale base revision before authorization or mutation", "[vibecut][runtime-protocol][stale]")
{
    Fixture fixture;
    VibeCutRuntimeProtocolAdapter adapter(&fixture.surface, [&fixture]() { return fixture.revision; });
    const QJsonObject stale = adapter.handleRequest(request(QStringLiteral("propose_plan"), plan(fixture.revision - 1, 1)));
    CHECK(errorPayload(stale).value(QStringLiteral("code")).toString() == QStringLiteral("stale_revision"));
    CHECK_FALSE(adapter.hasPendingPlan());
    CHECK(fixture.mutationCalls == 0);
}

TEST_CASE("protocol invoke cannot substitute approved tool input", "[vibecut][runtime-protocol][substitution]")
{
    Fixture fixture;
    VibeCutRuntimeProtocolAdapter adapter(&fixture.surface, [&fixture]() { return fixture.revision; });
    REQUIRE(adapter.handleRequest(request(QStringLiteral("propose_plan"), plan(fixture.revision, 1)))
                .value(QStringLiteral("type")).toString() == QStringLiteral("propose_plan"));
    const QJsonObject approved = adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    const QString auth = approved.value(QStringLiteral("payload")).toObject().value(QStringLiteral("authorization_id")).toString();

    const QJsonObject substituted = adapter.handleRequest(request(QStringLiteral("invoke"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("plan-1")},
                    {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("operation_id"), QStringLiteral("op-1")},
                    {QStringLiteral("expected_revision"), static_cast<qint64>(fixture.revision)},
                    {QStringLiteral("tool"), QStringLiteral("protocol_fake_mutate")},
                    {QStringLiteral("input"), QJsonObject{{QStringLiteral("value"), 999}}}}));
    CHECK(errorPayload(substituted).value(QStringLiteral("code")).toString() == QStringLiteral("plan_substitution_attempt"));
    CHECK(fixture.mutationCalls == 0);
}

TEST_CASE("protocol adapter carries moving expected revision across approved operations", "[vibecut][runtime-protocol][revision]")
{
    Fixture fixture;
    VibeCutRuntimeProtocolAdapter adapter(&fixture.surface, [&fixture]() { return fixture.revision; });
    adapter.handleRequest(request(QStringLiteral("propose_plan"), plan(fixture.revision, 2)));
    const QJsonObject approved = adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    const QJsonObject authPayload = approved.value(QStringLiteral("payload")).toObject();
    const QString auth = authPayload.value(QStringLiteral("authorization_id")).toString();
    quint64 expected = authPayload.value(QStringLiteral("expected_revision")).toVariant().toULongLong();
    REQUIRE(expected == 7);

    const QJsonObject first = adapter.handleRequest(request(QStringLiteral("invoke"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("plan-1")}, {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("operation_id"), QStringLiteral("op-1")}, {QStringLiteral("expected_revision"), static_cast<qint64>(expected)}}));
    const QJsonObject firstPayload = first.value(QStringLiteral("payload")).toObject();
    CHECK(firstPayload.value(QStringLiteral("ok")).toBool(false));
    expected = firstPayload.value(QStringLiteral("revision_after")).toVariant().toULongLong();
    CHECK(expected == 8);
    REQUIRE(fixture.observedInputs.size() == 1);
    CHECK(fixture.observedInputs.at(0).toObject().value(QStringLiteral("value")).toInt() == 1);

    const QJsonObject second = adapter.handleRequest(request(QStringLiteral("invoke"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("plan-1")}, {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("operation_id"), QStringLiteral("op-2")}, {QStringLiteral("expected_revision"), static_cast<qint64>(expected)}}));
    const QJsonObject secondPayload = second.value(QStringLiteral("payload")).toObject();
    CHECK(secondPayload.value(QStringLiteral("ok")).toBool(false));
    expected = secondPayload.value(QStringLiteral("revision_after")).toVariant().toULongLong();
    CHECK(expected == 9);
    CHECK(secondPayload.value(QStringLiteral("plan_complete_ready")).toBool(false));
    REQUIRE(fixture.observedInputs.size() == 2);
    CHECK(fixture.observedInputs.at(1).toObject().value(QStringLiteral("value")).toInt() == 2);

    const QJsonObject verified = adapter.handleRequest(request(QStringLiteral("verify"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("plan-1")}, {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("operation_id"), QStringLiteral("op-2")}, {QStringLiteral("expected_revision"), static_cast<qint64>(expected)},
                    {QStringLiteral("expected_postconditions"), QJsonArray{QStringLiteral("value advanced again")}},
                    {QStringLiteral("inspection"), QStringLiteral("protocol_fake_inspect")}, {QStringLiteral("inspection_input"), QJsonObject()}}));
    CHECK(verified.value(QStringLiteral("payload")).toObject().value(QStringLiteral("ok")).toBool(false));
    CHECK(fixture.mutationCalls == 2);

    const QJsonObject completed = adapter.handleRequest(request(QStringLiteral("complete_plan"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("plan-1")}, {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("expected_revision"), static_cast<qint64>(expected)}}));
    CHECK(completed.value(QStringLiteral("payload")).toObject().value(QStringLiteral("completed")).toBool(false));
    CHECK_FALSE(adapter.hasPendingPlan());
    CHECK_FALSE(adapter.hasAuthorization());
}

TEST_CASE("protocol adapter stops remaining work on unexpected revision drift", "[vibecut][runtime-protocol][drift]")
{
    Fixture fixture;
    VibeCutRuntimeProtocolAdapter adapter(&fixture.surface, [&fixture]() { return fixture.revision; });
    adapter.handleRequest(request(QStringLiteral("propose_plan"), plan(fixture.revision, 2)));
    const QJsonObject approved = adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    const QString auth = approved.value(QStringLiteral("payload")).toObject().value(QStringLiteral("authorization_id")).toString();
    const quint64 expected = adapter.expectedRevision();
    ++fixture.revision; // unrelated edit after authorization, before invoke

    const QJsonObject stale = adapter.handleRequest(request(QStringLiteral("invoke"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("plan-1")}, {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("operation_id"), QStringLiteral("op-1")}, {QStringLiteral("expected_revision"), static_cast<qint64>(expected)}}));
    CHECK(errorPayload(stale).value(QStringLiteral("code")).toString() == QStringLiteral("stale_revision"));
    CHECK_FALSE(adapter.hasAuthorization());
    CHECK(fixture.mutationCalls == 0);
}

TEST_CASE("protocol abort invalidates plan authority without claiming rollback parity", "[vibecut][runtime-protocol][abort]")
{
    Fixture fixture;
    VibeCutRuntimeProtocolAdapter adapter(&fixture.surface, [&fixture]() { return fixture.revision; });
    adapter.handleRequest(request(QStringLiteral("propose_plan"), plan(fixture.revision, 1)));
    const QJsonObject approved = adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    const QString auth = approved.value(QStringLiteral("payload")).toObject().value(QStringLiteral("authorization_id")).toString();

    const QJsonObject aborted = adapter.handleRequest(request(QStringLiteral("abort_plan"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("plan-1")}, {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("reason"), QStringLiteral("test abort")}}));
    const QJsonObject payload = aborted.value(QStringLiteral("payload")).toObject();
    CHECK(payload.value(QStringLiteral("aborted")).toBool(false));
    CHECK_FALSE(payload.value(QStringLiteral("rollback_performed")).toBool(true));
    CHECK_FALSE(adapter.hasPendingPlan());
    CHECK_FALSE(adapter.hasAuthorization());
    CHECK(fixture.mutationCalls == 0);
}
