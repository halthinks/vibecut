/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutembeddingstore.h"

#include <QFile>
#include <QTemporaryDir>

namespace {
VibeCutEmbeddingRecord record(const QString &anchor,
                              const QString &fingerprint,
                              const QVector<double> &vector,
                              const QString &producer = QStringLiteral("semantic_text_minilm"),
                              const QString &producerVersion = QStringLiteral("1.0.0"),
                              const QString &model = QStringLiteral("sentence-transformers/all-MiniLM-L6-v2"),
                              const QString &revision = QStringLiteral("rev-a"))
{
    VibeCutEmbeddingRecord item;
    item.anchorKind = QStringLiteral("evidence");
    item.anchorId = anchor;
    item.sourceId = QStringLiteral("bin:1");
    item.sourceFingerprint = fingerprint;
    item.modality = QStringLiteral("text");
    item.model = model;
    item.modelRevision = revision;
    item.producerId = producer;
    item.producerVersion = producerVersion;
    item.startFrame = 10;
    item.endFrame = 20;
    item.vector = vector;
    return item;
}
}

TEST_CASE("full embedding refresh atomically drops stale producer-model anchors and fingerprints", "[vibecut][embeddings][refresh]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("project.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();
    const QUrl url = QUrl::fromLocalFile(projectPath);

    QString error;
    REQUIRE(VibeCutEmbeddingStore::upsertForProjectUrl(url, record(QStringLiteral("old-a"), QStringLiteral("fp-old"), {1.0, 0.0}), &error));
    REQUIRE(VibeCutEmbeddingStore::upsertForProjectUrl(url, record(QStringLiteral("old-b"), QStringLiteral("fp-old"), {0.0, 1.0}), &error));
    REQUIRE(VibeCutEmbeddingStore::upsertForProjectUrl(url,
                                                       record(QStringLiteral("other-space"), QStringLiteral("fp-other"), {1.0, 0.0},
                                                              QStringLiteral("other-producer"), QStringLiteral("1.0.0"),
                                                              QStringLiteral("other/model"), QStringLiteral("other-rev")), &error));

    QList<VibeCutEmbeddingRecord> refreshed{
        record(QStringLiteral("current"), QStringLiteral("fp-current"), {1.0, 0.0},
               QStringLiteral("semantic_text_minilm"), QStringLiteral("2.0.0")),
    };
    REQUIRE(VibeCutEmbeddingStore::replaceProducerModelForProjectUrl(
        url, QStringLiteral("semantic_text_minilm"), QStringLiteral("2.0.0"),
        QStringLiteral("sentence-transformers/all-MiniLM-L6-v2"), QStringLiteral("rev-a"), refreshed, &error));
    REQUIRE(error.isEmpty());

    const QJsonObject root = VibeCutEmbeddingStore::loadForProjectUrl(url, &error);
    REQUIRE(error.isEmpty());
    const QJsonArray records = root.value(QStringLiteral("records")).toArray();
    REQUIRE(records.size() == 2);
    bool sawCurrent = false;
    bool sawOther = false;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        const QString anchor = object.value(QStringLiteral("anchor_id")).toString();
        CHECK(anchor != QStringLiteral("old-a"));
        CHECK(anchor != QStringLiteral("old-b"));
        if (anchor == QLatin1String("current")) {
            sawCurrent = true;
            CHECK(object.value(QStringLiteral("source_fingerprint")).toString() == QStringLiteral("fp-current"));
            CHECK(object.value(QStringLiteral("producer_version")).toString() == QStringLiteral("2.0.0"));
        }
        if (anchor == QLatin1String("other-space")) sawOther = true;
    }
    CHECK(sawCurrent);
    CHECK(sawOther);
}

TEST_CASE("embedding refresh rejects mixed dimensions duplicates and provenance mismatch before writing", "[vibecut][embeddings][refresh][integrity]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("project.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();
    const QUrl url = QUrl::fromLocalFile(projectPath);

    QString error;
    QList<VibeCutEmbeddingRecord> mixed{
        record(QStringLiteral("a"), QStringLiteral("fp"), {1.0, 0.0}),
        record(QStringLiteral("b"), QStringLiteral("fp"), {1.0, 0.0, 0.0}),
    };
    CHECK_FALSE(VibeCutEmbeddingStore::replaceProducerModelForProjectUrl(
        url, QStringLiteral("semantic_text_minilm"), QStringLiteral("1.0.0"),
        QStringLiteral("sentence-transformers/all-MiniLM-L6-v2"), QStringLiteral("rev-a"), mixed, &error));
    CHECK(error.contains(QStringLiteral("dimensions"), Qt::CaseInsensitive));

    error.clear();
    QList<VibeCutEmbeddingRecord> duplicate{
        record(QStringLiteral("a"), QStringLiteral("fp"), {1.0, 0.0}),
        record(QStringLiteral("a"), QStringLiteral("fp"), {0.0, 1.0}),
    };
    CHECK_FALSE(VibeCutEmbeddingStore::replaceProducerModelForProjectUrl(
        url, QStringLiteral("semantic_text_minilm"), QStringLiteral("1.0.0"),
        QStringLiteral("sentence-transformers/all-MiniLM-L6-v2"), QStringLiteral("rev-a"), duplicate, &error));
    CHECK(error.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));

    error.clear();
    VibeCutEmbeddingRecord wrong = record(QStringLiteral("a"), QStringLiteral("fp"), {1.0, 0.0});
    wrong.modelRevision = QStringLiteral("wrong");
    CHECK_FALSE(VibeCutEmbeddingStore::replaceProducerModelForProjectUrl(
        url, QStringLiteral("semantic_text_minilm"), QStringLiteral("1.0.0"),
        QStringLiteral("sentence-transformers/all-MiniLM-L6-v2"), QStringLiteral("rev-a"), {wrong}, &error));
    CHECK(error.contains(QStringLiteral("model/revision"), Qt::CaseInsensitive));
}
