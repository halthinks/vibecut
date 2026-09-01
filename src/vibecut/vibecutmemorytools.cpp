/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutmemorytools.h"

#include "vibecutprojectmemory.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
}

bool registerVibeCutMemoryTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{{QStringLiteral("name"), QStringLiteral("project_memory_list")},
                                 {QStringLiteral("description"), QStringLiteral("List bounded durable VibeCut project-memory entries from .vibecutmemory.json. Read-only. These are fallible remembered facts, not live timeline evidence.")},
                                 {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("project_memory_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy, [](const QJsonObject &) {
            QString errorMessage;
            const QJsonArray entries = VibeCutProjectMemory::loadCurrent(&errorMessage);
            if (!errorMessage.isEmpty()) return err(errorMessage);
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("entries"), entries}};
        }, error)) {
        return false;
    }

    const QJsonObject putInput{{QStringLiteral("type"), QStringLiteral("object")},
                               {QStringLiteral("properties"), QJsonObject{
                                   {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                   {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                        {QStringLiteral("description"), QStringLiteral("Short provenance label such as user, agent, import, review.")}}}}},
                               {QStringLiteral("required"), QJsonArray{QStringLiteral("text")}},
                               {QStringLiteral("additionalProperties"), false}};
    const QJsonObject putSchema{{QStringLiteral("name"), QStringLiteral("project_memory_put")},
                                {QStringLiteral("description"), QStringLiteral("Persist one bounded project-memory fact beside the saved project. This writes .vibecutmemory.json and therefore goes through VibeCut side-effect governance. Do not store secrets or transient live-state observations.")},
                                {QStringLiteral("input_schema"), putInput}};
    VibeCutToolPolicy putPolicy;
    putPolicy.name = QStringLiteral("project_memory_put");
    putPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    putPolicy.mutatesProject = false;
    if (!surface.registerTool(putSchema, putPolicy, [](const QJsonObject &input) {
            QString id;
            QString errorMessage;
            if (!VibeCutProjectMemory::putCurrent(input.value(QStringLiteral("text")).toString(),
                                                  input.value(QStringLiteral("source")).toString(QStringLiteral("agent")),
                                                  &id, &errorMessage)) {
                return err(errorMessage);
            }
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("memory_id"), id}, {QStringLiteral("persisted"), true}};
        }, error)) {
        return false;
    }

    const QJsonObject forgetInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("memory_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("memory_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject forgetSchema{{QStringLiteral("name"), QStringLiteral("project_memory_forget")},
                                   {QStringLiteral("description"), QStringLiteral("Forget one durable project-memory entry by id. This edits .vibecutmemory.json and is governed as an external side effect.")},
                                   {QStringLiteral("input_schema"), forgetInput}};
    VibeCutToolPolicy forgetPolicy;
    forgetPolicy.name = QStringLiteral("project_memory_forget");
    forgetPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    forgetPolicy.mutatesProject = false;
    return surface.registerTool(forgetSchema, forgetPolicy, [](const QJsonObject &input) {
        QString errorMessage;
        const QString id = input.value(QStringLiteral("memory_id")).toString();
        if (!VibeCutProjectMemory::forgetCurrent(id, &errorMessage)) return err(errorMessage);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("memory_id"), id}, {QStringLiteral("forgotten"), true}};
    }, error);
}
