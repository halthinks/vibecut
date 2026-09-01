/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutmediaevidencetools.h"

#include "vibecutmediaevidence.h"
#include "vibecutsourceextractortools.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject summary(const QJsonObject &)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return err(error);

    QHash<QString, int> byKind;
    QHash<QString, int> byExtractor;
    QHash<QString, int> bySource;
    int withConfidence = 0;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        ++byKind[object.value(QStringLiteral("kind")).toString()];
        ++byExtractor[object.value(QStringLiteral("extractor_id")).toString()];
        ++bySource[object.value(QStringLiteral("source_id")).toString()];
        if (object.value(QStringLiteral("confidence")).toDouble(-1.0) >= 0.0) ++withConfidence;
    }

    auto counts = [](const QHash<QString, int> &source) {
        QJsonObject result;
        for (auto it = source.constBegin(); it != source.constEnd(); ++it) result.insert(it.key(), it.value());
        return result;
    };

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("schema_version"), VibeCutMediaEvidence::SchemaVersion},
                       {QStringLiteral("record_count"), records.size()},
                       {QStringLiteral("source_count"), bySource.size()},
                       {QStringLiteral("extractor_count"), byExtractor.size()},
                       {QStringLiteral("records_with_confidence"), withConfidence},
                       {QStringLiteral("by_kind"), counts(byKind)},
                       {QStringLiteral("by_extractor"), counts(byExtractor)}};
}

QJsonObject listRecords(const QJsonObject &input)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return err(error);

    const QString sourceId = input.value(QStringLiteral("source_id")).toString().trimmed();
    const QString extractorId = input.value(QStringLiteral("extractor_id")).toString().trimmed();
    const QString kind = input.value(QStringLiteral("kind")).toString().trimmed();
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(100), 1000);

    QJsonArray matches;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        if (!sourceId.isEmpty() && object.value(QStringLiteral("source_id")).toString() != sourceId) continue;
        if (!extractorId.isEmpty() && object.value(QStringLiteral("extractor_id")).toString() != extractorId) continue;
        if (!kind.isEmpty() && object.value(QStringLiteral("kind")).toString() != kind) continue;
        matches.append(object);
        if (matches.size() >= limit) break;
    }

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("record_count"), matches.size()},
                       {QStringLiteral("records"), matches},
                       {QStringLiteral("filters"), QJsonObject{{QStringLiteral("source_id"), sourceId},
                                                                {QStringLiteral("extractor_id"), extractorId},
                                                                {QStringLiteral("kind"), kind}}}};
}
} // namespace

bool registerVibeCutMediaEvidenceTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy summaryPolicy;
    summaryPolicy.name = QStringLiteral("media_evidence_summary");
    summaryPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), summaryPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Summarize the persistent extractor-produced media evidence ledger by source, extractor and evidence kind. Read-only; the model cannot write evidence through this surface.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              summaryPolicy, summary, error)) return false;

    const QJsonObject listInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{
                                    {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                    {QStringLiteral("extractor_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                    {QStringLiteral("kind"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                    {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}}}},
                                {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("media_evidence_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), listPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("List persistent extractor-produced evidence records with optional source/extractor/kind filters, including provenance, confidence and source fingerprint. Read-only.")},
                                          {QStringLiteral("input_schema"), listInput}},
                              listPolicy, listRecords, error)) return false;

    return registerVibeCutSourceExtractorTools(surface, error);
}
