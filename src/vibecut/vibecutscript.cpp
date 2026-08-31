/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutscript.h"

#include "vibecutplanruntime.h"
#include "vibecuttoolsurface.h"

#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonValue>

#include <atomic>
#include <chrono>
#include <thread>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
}

VibeCutScriptSandbox::Result VibeCutScriptSandbox::evaluatePlan(const QString &source, int timeoutMs)
{
    Result result;
    const QString script = source.trimmed();
    if (script.isEmpty()) {
        result.error = QStringLiteral("VibeScript source must not be empty.");
        return result;
    }
    if (script.size() > 65536) {
        result.error = QStringLiteral("VibeScript source exceeds the 64 KiB sandbox limit.");
        return result;
    }
    timeoutMs = qBound(25, timeoutMs, 2000);

    QJSEngine engine;
    // Deliberately expose no QObject, filesystem, process, network, editor,
    // project or host application object. The script only has standard
    // ECMAScript primitives and must return a JSON-serializable plan object.
    std::atomic<bool> finished(false);
    std::thread watchdog([&engine, &finished, timeoutMs]() {
        const int sleepMs = 10;
        int elapsed = 0;
        while (!finished.load(std::memory_order_acquire) && elapsed < timeoutMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            elapsed += sleepMs;
        }
        if (!finished.load(std::memory_order_acquire)) {
            engine.setInterrupted(true);
        }
    });

    QStringList stack;
    QJSValue value = engine.evaluate(script, QStringLiteral("vibescript"), 1, &stack);
    finished.store(true, std::memory_order_release);
    watchdog.join();

    if (engine.isInterrupted()) {
        result.timedOut = true;
        result.error = QStringLiteral("VibeScript exceeded the %1 ms execution limit.").arg(timeoutMs);
        return result;
    }
    if (value.isError()) {
        QString detail = value.toString();
        if (!stack.isEmpty()) detail += QStringLiteral(" — ") + stack.join(QStringLiteral(" | "));
        result.error = QStringLiteral("VibeScript error: %1").arg(detail.left(2048));
        return result;
    }

    const QJsonValue json = QJsonValue::fromVariant(value.toVariant());
    if (!json.isObject()) {
        result.error = QStringLiteral("VibeScript must return one JSON object with objective and operations.");
        return result;
    }
    const QJsonObject object = json.toObject();
    if (object.value(QStringLiteral("objective")).toString().trimmed().isEmpty()) {
        result.error = QStringLiteral("VibeScript plan requires a non-empty objective.");
        return result;
    }
    if (!object.value(QStringLiteral("operations")).isArray() || object.value(QStringLiteral("operations")).toArray().isEmpty()) {
        result.error = QStringLiteral("VibeScript plan requires a non-empty operations array.");
        return result;
    }

    result.ok = true;
    result.value = object;
    return result;
}

bool registerVibeCutScriptTools(VibeCutToolSurface &surface, VibeCutPlanRuntime *runtime, QString *error)
{
    if (!runtime) {
        if (error) *error = QStringLiteral("VibeScript requires a plan runtime.");
        return false;
    }

    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                            {QStringLiteral("description"), QStringLiteral("JavaScript expression/program that returns a JSON plan object: {objective, operations:[{id,tool,input,depends_on?}]}. No host APIs are exposed.")}}},
                                      {QStringLiteral("timeout_ms"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                                {QStringLiteral("minimum"), 25},
                                                                                {QStringLiteral("maximum"), 2000}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("source")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("vibescript_plan")},
                             {QStringLiteral("description"), QStringLiteral("Evaluate bounded sandboxed JavaScript that can only compute a JSON edit plan. The script receives no filesystem, network, process, QObject or Kdenlive access. A valid result is submitted to the normal governed VibeCut plan runtime; the script itself never mutates the project.")},
                             {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("vibescript_plan");
    policy.risk = VibeCutToolRisk::ReadOnly;

    return surface.registerTool(schema, policy, [runtime](const QJsonObject &input) {
        const VibeCutScriptSandbox::Result evaluated = VibeCutScriptSandbox::evaluatePlan(
            input.value(QStringLiteral("source")).toString(), input.value(QStringLiteral("timeout_ms")).toInt(250));
        if (!evaluated.ok) {
            return err(evaluated.error);
        }
        QJsonObject proposed = runtime->propose(evaluated.value);
        proposed.insert(QStringLiteral("vibescript"), true);
        return proposed;
    }, error);
}
