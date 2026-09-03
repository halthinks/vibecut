/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutembeddingstore.h"

#include <QFile>
#include <QTemporaryDir>

namespace {
VibeCutEmbeddingRecord embedding(const QString &anchor,
                                 const QVector<double> &vector,
                                 const QString &model = QStringLiteral("test/shared-model"),
                                 const QString &revision = QStringLiteral("rev-a"),
                                 const QString &modality = QStringLiteral("text"),
                                 const QString &fingerprint = QStringLiteral("fp-a"))
{
    VibeCutEmbeddingRecord record;
    record.anchorKind = QStringLiteral("evidence");
    record.anchorId = anchor;
    record.sourceId = QStringLiteral("bin:1");
    record.sourceFingerprint = fingerprint;
    record.modality = modality;
    record.model = model;
    record.modelRevision = revision;
    record.producerId = QStringLiteral("test_embedder");
    record.producerVersion = QStringLiteral("1.0.0");
    record.startFrame = 10;
    record.endFrame = 20;
    record.vector = vector;
    record.metadata = QJsonObject{{QStringLiteral("authority"), QStringLiteral("model_representation")}};
    return record;
}
}

TEST_CASE("embedding vectors are explicitly normalized before persistence", "[vibecut][embeddings]")
{
    QVector<double> unit;
    QString error;
    REQUIRE(VibeCutEmbeddingStore::normalizeVector(QVector<double>{3.0, 4.0}, unit, &error));
    CHECK(error.isEmpty());
    REQUIRE(unit.size() == 2);
    CHECK(unit.at(0) == Approx(0.6).epsilon(1e-9));
    CHECK(unit.at(1) == Approx(0.8).epsilon(1e-9));

    QVector<double> rejected;
    CHECK_FALSE(VibeCutEmbeddingStore::normalizeVector(QVector<double>{0.0, 0.0}, rejected, &error));
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("embedding sidecar fails closed on malformed or non-unit vectors", "[vibecut][embeddings][integrity]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("project.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();
    const QUrl projectUrl = QUrl::fromLocalFile(projectPath);

    QString error;
    VibeCutEmbeddingRecord bad = embedding(QStringLiteral("a"), QVector<double>{1.0, 1.0});
    CHECK_FALSE(VibeCutEmbeddingStore::upsertForProjectUrl(projectUrl, bad, &error));
    CHECK(error.contains(QStringLiteral("unit-normalized"), Qt::CaseInsensitive));

    QVector<double> unit;
    REQUIRE(VibeCutEmbeddingStore::normalizeVector(QVector<double>{1.0, 1.0}, unit, &error));
    REQUIRE(VibeCutEmbeddingStore::upsertForProjectUrl(projectUrl, embedding(QStringLiteral("a"), unit), &error));
    CHECK(error.isEmpty());

    QFile sidecar(dir.filePath(VibeCutEmbeddingStore::fileName()));
    REQUIRE(sidecar.open(QIODevice::ReadOnly));
    QByteArray data = sidecar.readAll();
    sidecar.close();
    data.replace("\"unit_normalized\":true", "\"unit_normalized\":false");
    REQUIRE(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(sidecar.write(data) == data.size());
    sidecar.close();

    const QJsonObject rejected = VibeCutEmbeddingStore::loadForProjectUrl(projectUrl, &error);
    CHECK(rejected.isEmpty());
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("embedding upsert replaces only the same provenance slice", "[vibecut][embeddings][provenance]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("project.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();
    const QUrl projectUrl = QUrl::fromLocalFile(projectPath);

    QString error;
    REQUIRE(VibeCutEmbeddingStore::upsertForProjectUrl(projectUrl,
                                                       embedding(QStringLiteral("a"), QVector<double>{1.0, 0.0}), &error));
    REQUIRE(VibeCutEmbeddingStore::upsertForProjectUrl(projectUrl,
                                                       embedding(QStringLiteral("a"), QVector<double>{0.0, 1.0}), &error));
    REQUIRE(VibeCutEmbeddingStore::upsertForProjectUrl(projectUrl,
                                                       embedding(QStringLiteral("a"), QVector<double>{1.0, 0.0},
                                                                 QStringLiteral("test/shared-model"), QStringLiteral("rev-a"),
                                                                 QStringLiteral("text"), QStringLiteral("fp-b")), &error));

    const QJsonObject root = VibeCutEmbeddingStore::loadForProjectUrl(projectUrl, &error);
    REQUIRE(error.isEmpty());
    const QJsonArray records = root.value(QStringLiteral("records")).toArray();
    REQUIRE(records.size() == 2);
    bool sawA = false;
    bool sawB = false;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        const QString fingerprint = object.value(QStringLiteral("source_fingerprint")).toString();
        if (fingerprint == QLatin1String("fp-a")) {
            sawA = true;
            const QJsonArray vector = object.value(QStringLiteral("vector")).toArray();
            CHECK(vector.at(0).toDouble() == Approx(0.0));
            CHECK(vector.at(1).toDouble() == Approx(1.0));
        }
        if (fingerprint == QLatin1String("fp-b")) sawB = true;
    }
    CHECK(sawA);
    CHECK(sawB);
}

TEST_CASE("cosine search compares only exact compatible embedding spaces", "[vibecut][embeddings][search]")
{
    QJsonArray records;
    records.append(embedding(QStringLiteral("near"), QVector<double>{1.0, 0.0}).toJson());
    records.append(embedding(QStringLiteral("orthogonal"), QVector<double>{0.0, 1.0}).toJson());
    records.append(embedding(QStringLiteral("other-revision"), QVector<double>{1.0, 0.0},
                             QStringLiteral("test/shared-model"), QStringLiteral("rev-b")).toJson());
    records.append(embedding(QStringLiteral("visual"), QVector<double>{0.8, 0.6},
                             QStringLiteral("test/shared-model"), QStringLiteral("rev-a"), QStringLiteral("visual")).toJson());
    const QJsonObject root{{QStringLiteral("version"), VibeCutEmbeddingStore::SchemaVersion},
                           {QStringLiteral("records"), records}};

    QString error;
    const QList<VibeCutEmbeddingSearchHit> hits = VibeCutEmbeddingStore::cosineSearch(
        root, QVector<double>{1.0, 0.0}, QStringLiteral("test/shared-model"), QStringLiteral("rev-a"),
        QStringList{QStringLiteral("text")}, 10, -1.0, &error);
    REQUIRE(error.isEmpty());
    REQUIRE(hits.size() == 2);
    CHECK(hits.at(0).anchorId == QStringLiteral("near"));
    CHECK(hits.at(0).similarity == Approx(1.0));
    CHECK(hits.at(1).anchorId == QStringLiteral("orthogonal"));
    CHECK(hits.at(1).similarity == Approx(0.0));

    const QList<VibeCutEmbeddingSearchHit> crossModal = VibeCutEmbeddingStore::cosineSearch(
        root, QVector<double>{1.0, 0.0}, QStringLiteral("test/shared-model"), QStringLiteral("rev-a"),
        QStringList(), 10, 0.5, &error);
    REQUIRE(error.isEmpty());
    REQUIRE(crossModal.size() == 2);
    CHECK(crossModal.at(0).anchorId == QStringLiteral("near"));
    CHECK(crossModal.at(1).anchorId == QStringLiteral("visual"));
    CHECK(crossModal.at(1).similarity == Approx(0.8).epsilon(1e-9));
}
