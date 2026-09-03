/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutmediatools.h"

#include "vibecutmediaindex.h"
#include "vibecutsemantictools.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QJsonObject mediaSearch(const QJsonObject &input)
{
    const QString query = input.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("query must not be empty")}};
    }
    VibeCutMediaIndex index;
    QString error;
    if (!index.rebuildFromCurrentProject(&error)) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    const QList<VibeCutMediaSearchHit> hits = index.search(query, input.value(QStringLiteral("limit")).toInt(25));
    QJsonArray jsonHits;
    for (const VibeCutMediaSearchHit &hit : hits) jsonHits.append(hit.toJson());
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("query"), query},
                       {QStringLiteral("indexed_documents"), index.size()}, {QStringLiteral("hits"), jsonHits}};
}
}

bool registerVibeCutMediaTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                           {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_search")},
                             {QStringLiteral("description"), QStringLiteral("Deterministically search the active project's canonical media knowledge index across transcript/subtitle text, clip names and textual extractor evidence. Returns ranked, time-ranged evidence. Read-only. Use semantic_search_text for conceptual transcript/OCR similarity after semantic_text_refresh; this lexical path remains available independently of ML embeddings.")},
                             {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_search");
    policy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(schema, policy, mediaSearch, error)) return false;
    return registerVibeCutSemanticTools(surface, error);
}
