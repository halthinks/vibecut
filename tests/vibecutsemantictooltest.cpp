/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"

#include "vibecut/vibecutembeddingstore.h"
#include "vibecut/vibecutmediatools.h"
#include "vibecut/vibecutsemantictools.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

#include <QCryptographicHash>

namespace {
QString hashText(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

VibeCutMediaDocument document(const QString &id, const QString &text, const QString &source, const QString &fingerprint,
                              int start = 10, int end = 20, const QString &kind = QStringLiteral("transcript"))
{
    VibeCutMediaDocument doc;
    doc.id = id;
    doc.kind = kind;
    doc.text = text;
    doc.startFrame = start;
    doc.endFrame = end;
    doc.metadata = QJsonObject{{QStringLiteral("source_id"), source},
                               {QStringLiteral("source_fingerprint"), fingerprint}};
    return doc;
}

VibeCutEmbeddingRecord recordFor(const VibeCutMediaDocument &doc)
{
    VibeCutEmbeddingRecord record;
    record.anchorKind = doc.kind;
    record.anchorId = doc.id;
    record.sourceId = doc.metadata.value(QStringLiteral("source_id")).toString();
    record.sourceFingerprint = doc.metadata.value(QStringLiteral("source_fingerprint")).toString();
    record.modality = QStringLiteral("text");
    record.model = QStringLiteral("sentence-transformers/all-MiniLM-L6-v2");
    record.modelRevision = QStringLiteral("1110a243fdf4706b3f48f1d95db1a4f5529b4d41");
    record.producerId = QStringLiteral("semantic_text_minilm");
    record.producerVersion = QStringLiteral("1.0.0");
    record.startFrame = doc.startFrame;
    record.endFrame = doc.endFrame;
    record.vector = QVector<double>(384, 0.0);
    record.vector[0] = 1.0;
    record.metadata = QJsonObject{{QStringLiteral("text_sha256"), hashText(doc.text.trimmed())}};
    return record;
}
}

TEST_CASE("semantic current-only filter requires exact anchor source range and full text identity", "[vibecut][semantic][freshness]")
{
    const VibeCutMediaDocument current = document(QStringLiteral("doc-a"), QStringLiteral("current transcript"),
                                                  QStringLiteral("bin:1"), QStringLiteral("fp-current"));
    const VibeCutEmbeddingRecord good = recordFor(current);

    VibeCutEmbeddingRecord staleText = good;
    staleText.anchorId = QStringLiteral("doc-text");
    staleText.metadata.insert(QStringLiteral("text_sha256"), hashText(QStringLiteral("old transcript")));
    VibeCutMediaDocument currentText = current;
    currentText.id = staleText.anchorId;

    VibeCutEmbeddingRecord staleFingerprint = good;
    staleFingerprint.anchorId = QStringLiteral("doc-fp");
    staleFingerprint.sourceFingerprint = QStringLiteral("fp-old");
    VibeCutMediaDocument currentFingerprint = current;
    currentFingerprint.id = staleFingerprint.anchorId;

    VibeCutEmbeddingRecord staleSource = good;
    staleSource.anchorId = QStringLiteral("doc-source");
    staleSource.sourceId = QStringLiteral("bin:old");
    VibeCutMediaDocument currentSource = current;
    currentSource.id = staleSource.anchorId;

    VibeCutEmbeddingRecord staleRange = good;
    staleRange.anchorId = QStringLiteral("doc-range");
    staleRange.endFrame = 21;
    VibeCutMediaDocument currentRange = current;
    currentRange.id = staleRange.anchorId;

    VibeCutEmbeddingRecord staleKind = good;
    staleKind.anchorId = QStringLiteral("doc-kind");
    staleKind.anchorKind = QStringLiteral("ocr_text");
    VibeCutMediaDocument currentKind = current;
    currentKind.id = staleKind.anchorId;

    VibeCutEmbeddingRecord removed = good;
    removed.anchorId = QStringLiteral("removed-doc");

    QJsonObject root = VibeCutEmbeddingStore::emptyRoot();
    root.insert(QStringLiteral("records"), QJsonArray{good.toJson(), staleText.toJson(), staleFingerprint.toJson(),
                                                       staleSource.toJson(), staleRange.toJson(), staleKind.toJson(), removed.toJson()});
    int staleSkipped = -1;
    QString error;
    const QJsonObject filtered = filterVibeCutCurrentSemanticTextEmbeddingRoot(
        root, QList<VibeCutMediaDocument>{current, currentText, currentFingerprint, currentSource, currentRange, currentKind},
        &staleSkipped, &error);
    REQUIRE(error.isEmpty());
    CHECK(staleSkipped == 6);
    const QJsonArray records = filtered.value(QStringLiteral("records")).toArray();
    REQUIRE(records.size() == 1);
    CHECK(records.at(0).toObject().value(QStringLiteral("anchor_id")).toString() == QStringLiteral("doc-a"));
}

TEST_CASE("semantic current-only filter ignores unrelated embedding spaces without counting them stale", "[vibecut][semantic][freshness][spaces]")
{
    const VibeCutMediaDocument current = document(QStringLiteral("doc-a"), QStringLiteral("text"),
                                                  QStringLiteral("bin:1"), QStringLiteral("fp"));
    VibeCutEmbeddingRecord good = recordFor(current);
    VibeCutEmbeddingRecord otherProducer = good;
    otherProducer.anchorId = QStringLiteral("other-producer");
    otherProducer.producerId = QStringLiteral("another_producer");
    VibeCutEmbeddingRecord visual = good;
    visual.anchorId = QStringLiteral("visual");
    visual.modality = QStringLiteral("image");

    QJsonObject root = VibeCutEmbeddingStore::emptyRoot();
    root.insert(QStringLiteral("records"), QJsonArray{good.toJson(), otherProducer.toJson(), visual.toJson()});
    int staleSkipped = -1;
    QString error;
    const QJsonObject filtered = filterVibeCutCurrentSemanticTextEmbeddingRoot(root, QList<VibeCutMediaDocument>{current},
                                                                               &staleSkipped, &error);
    REQUIRE(error.isEmpty());
    CHECK(staleSkipped == 0);
    CHECK(filtered.value(QStringLiteral("records")).toArray().size() == 1);
}

TEST_CASE("semantic current-only filter fails closed on duplicate current document ids", "[vibecut][semantic][freshness][integrity]")
{
    const VibeCutMediaDocument current = document(QStringLiteral("dup"), QStringLiteral("text"), QString(), QString());
    QJsonObject root = VibeCutEmbeddingStore::emptyRoot();
    QString error;
    CHECK(filterVibeCutCurrentSemanticTextEmbeddingRoot(root, QList<VibeCutMediaDocument>{current, current}, nullptr, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
}

TEST_CASE("semantic tools register beside deterministic lexical media search", "[vibecut][semantic][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QString error;
    REQUIRE(registerVibeCutMediaTools(surface, &error));
    CHECK(error.isEmpty());

    const auto policies = surface.policies();
    for (const QString &name : {QStringLiteral("media_search"), QStringLiteral("semantic_status"),
                                QStringLiteral("semantic_setup"), QStringLiteral("semantic_text_refresh"),
                                QStringLiteral("semantic_search_text"), QStringLiteral("semantic_result")}) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
    }

    CHECK(policies.value(QStringLiteral("media_search")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("semantic_status")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("semantic_result")).risk == VibeCutToolRisk::ReadOnly);

    const VibeCutToolPolicy search = policies.value(QStringLiteral("semantic_search_text"));
    CHECK(search.risk == VibeCutToolRisk::ReadOnly);
    CHECK(search.asynchronous);
    CHECK_FALSE(search.mutatesProject);

    const VibeCutToolPolicy refresh = policies.value(QStringLiteral("semantic_text_refresh"));
    CHECK(refresh.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(refresh.asynchronous);
    CHECK_FALSE(refresh.mutatesProject);

    const VibeCutToolPolicy setup = policies.value(QStringLiteral("semantic_setup"));
    CHECK(setup.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(setup.asynchronous);
    CHECK(setup.confirmationRequired);
    CHECK(setup.requiresConfirmation(VibeCutTrustMode::Turbo));
    CHECK_FALSE(setup.mutatesProject);
}

TEST_CASE("semantic schemas expose bounded policy without model path or vector injection", "[vibecut][semantic][schema]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QString error;
    REQUIRE(registerVibeCutMediaTools(surface, &error));

    QJsonObject refreshSchema;
    QJsonObject searchSchema;
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        const QString name = schema.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("semantic_text_refresh")) refreshSchema = schema;
        if (name == QLatin1String("semantic_search_text")) searchSchema = schema;
    }
    REQUIRE_FALSE(refreshSchema.isEmpty());
    REQUIRE_FALSE(searchSchema.isEmpty());

    const QJsonObject refreshProperties = refreshSchema.value(QStringLiteral("input_schema")).toObject()
                                              .value(QStringLiteral("properties")).toObject();
    CHECK(refreshProperties.contains(QStringLiteral("device")));
    CHECK(refreshProperties.contains(QStringLiteral("batch_size")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("model")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("model_revision")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("vector")));

    const QJsonObject searchProperties = searchSchema.value(QStringLiteral("input_schema")).toObject()
                                             .value(QStringLiteral("properties")).toObject();
    CHECK(searchProperties.contains(QStringLiteral("query")));
    CHECK(searchProperties.contains(QStringLiteral("limit")));
    CHECK(searchProperties.contains(QStringLiteral("min_similarity")));
    CHECK(searchProperties.contains(QStringLiteral("device")));
    CHECK_FALSE(searchProperties.contains(QStringLiteral("model")));
    CHECK_FALSE(searchProperties.contains(QStringLiteral("embedding")));
    CHECK_FALSE(searchProperties.contains(QStringLiteral("vector")));
}
