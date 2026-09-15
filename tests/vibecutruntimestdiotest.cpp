/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"

#include "vibecut/vibecutruntimestdiotransport.h"
#include "vibecut/vibecutruntimeprotocoladapter.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

#include <QEventLoop>
#include <QStandardPaths>
#include <QTimer>

namespace {
QJsonObject startupPlan(quint64 revision)
{
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("stdio-order-plan")},
        {QStringLiteral("base_revision"), static_cast<qint64>(revision)},
        {QStringLiteral("objective"), QStringLiteral("Prove external runtime startup ordering")},
        {QStringLiteral("operations"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("op-1")},
                                {QStringLiteral("tool"), QStringLiteral("stdio_order_fixture")},
                                {QStringLiteral("input"), QJsonObject{}},
                                {QStringLiteral("depends_on"), QJsonArray{}},
                                {QStringLiteral("expected_postconditions"), QJsonArray{}}}}}};
}
} // namespace

TEST_CASE("runtime stdio startup sends hello before exact plan handoff and fails closed on disconnect",
          "[vibecut][runtime-stdio][startup]")
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    REQUIRE_FALSE(python.isEmpty());

    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    quint64 revision = 17;

    const QJsonObject schema{
        {QStringLiteral("name"), QStringLiteral("stdio_order_fixture")},
        {QStringLiteral("description"), QStringLiteral("stdio startup ordering fixture")},
        {QStringLiteral("input_schema"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                     {QStringLiteral("properties"), QJsonObject{}},
                     {QStringLiteral("additionalProperties"), false}}}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("stdio_order_fixture");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    QString error;
    REQUIRE(surface.registerTool(schema, policy, [](const QJsonObject &) {
        return QJsonObject{{QStringLiteral("ok"), true}};
    }, &error));
    REQUIRE(error.isEmpty());

    VibeCutRuntimeProtocolAdapter adapter(&surface, [&revision]() { return revision; });
    VibeCutRuntimeStdioTransport transport(&adapter);

    QStringList diagnostics;
    bool stopped = false;
    int exitCode = -1;
    int exitStatus = -1;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&transport, &VibeCutRuntimeStdioTransport::diagnostic, &loop,
                     [&diagnostics](const QString &message) { diagnostics.append(message); });
    QObject::connect(&transport, &VibeCutRuntimeStdioTransport::stopped, &loop,
                     [&loop, &stopped, &exitCode, &exitStatus](int code, int status) {
        stopped = true;
        exitCode = code;
        exitStatus = status;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    const QString childScript = QStringLiteral(
        "import json,sys\n"
        "hello=json.loads(sys.stdin.readline())\n"
        "handoff=json.loads(sys.stdin.readline())\n"
        "assert hello.get('type') == 'hello', hello\n"
        "assert handoff.get('type') == 'plan_handoff', handoff\n"
        "assert handoff.get('kind') == 'event', handoff\n"
        "assert handoff['payload']['plan']['id'] == 'stdio-order-plan', handoff\n"
        "assert handoff['payload']['project_revision'] == 17, handoff\n"
        "sys.stderr.write('VIBECUT_STDIO_ORDER_OK\\n')\n"
        "sys.stderr.flush()\n");

    REQUIRE(transport.start(python, QStringList{QStringLiteral("-u"), QStringLiteral("-c"), childScript},
                            VibeCutTrustMode::Off, &error));
    REQUIRE(error.isEmpty());
    REQUIRE(transport.waitUntilReady(3000, &error));
    REQUIRE(error.isEmpty());
    REQUIRE(transport.handoffPlan(startupPlan(revision), &error));
    REQUIRE(error.isEmpty());
    REQUIRE(adapter.hasPendingPlan());
    CHECK_FALSE(adapter.hasAuthorization());

    if (!stopped) {
        timeout.start(5000);
        loop.exec();
    }

    CHECK(stopped);
    CHECK(exitCode == 0);
    CHECK(exitStatus == 0);
    CHECK(diagnostics.join(QLatin1Char('\n')).contains(QStringLiteral("VIBECUT_STDIO_ORDER_OK")));
    CHECK_FALSE(adapter.hasPendingPlan());
    CHECK_FALSE(adapter.hasAuthorization());
}
