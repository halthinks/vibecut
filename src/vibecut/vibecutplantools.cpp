/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutplantools.h"

#include "vibecutplanruntime.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

bool registerVibeCutPlanTools(VibeCutToolSurface &surface, VibeCutPlanRuntime *runtime, QString *error)
{
    if (!runtime) {
        if (error) {
            *error = QStringLiteral("edit_plan_propose requires a plan runtime");
        }
        return false;
    }

    const QJsonObject operationSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("id"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("description"), QStringLiteral("Unique stable step id within this plan, e.g. 'clean-audio'.")}}},
             {QStringLiteral("tool"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("description"), QStringLiteral("Exact governed VibeCut tool name to execute after approval.")}}},
             {QStringLiteral("input"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                          {QStringLiteral("description"), QStringLiteral("Arguments for that tool exactly as its schema expects.")}}},
             {QStringLiteral("depends_on"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                          {QStringLiteral("description"), QStringLiteral("Step ids that must complete successfully first.")}}},
             {QStringLiteral("expected_postconditions"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                          {QStringLiteral("description"), QStringLiteral("Concrete state/evidence expected after this step.")}}},
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("id"), QStringLiteral("tool"), QStringLiteral("input")}},
        {QStringLiteral("additionalProperties"), false},
    };

    const QJsonObject inputSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("objective"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("description"), QStringLiteral("Short description of the user-visible editing outcome.")}}},
             {QStringLiteral("operations"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("items"), operationSchema},
                          {QStringLiteral("minItems"), 1},
                          {QStringLiteral("description"), QStringLiteral("Complete mutation/side-effect plan. Read-only investigation should not be included.")}}},
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("objective"), QStringLiteral("operations")}},
        {QStringLiteral("additionalProperties"), false},
    };

    const QJsonObject schema{
        {QStringLiteral("name"), QStringLiteral("edit_plan_propose")},
        {QStringLiteral("description"),
         QStringLiteral("Propose the complete editor-changing plan for user review. Call this BEFORE any tool that edits the project, starts external setup/publishing, or has another side effect. Read-only inspection tools may run first. This tool itself never changes the project; it captures the current project revision and waits for explicit approval.")},
        {QStringLiteral("input_schema"), inputSchema},
    };

    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("edit_plan_propose");
    policy.risk = VibeCutToolRisk::ReadOnly;

    return surface.registerTool(schema, policy, [runtime](const QJsonObject &input) { return runtime->propose(input); }, error);
}
