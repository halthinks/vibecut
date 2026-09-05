/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutmediaevidence.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

TEST_CASE("media evidence records round-trip and validate", "[vibecut][media][evidence]")
{
    VibeCutMediaEvidenceRecord record;
    record.id = QStringLiteral("silence:1");
    record.sourceId = QStringLiteral("bin:1");
    record.sourceFingerprint = QStringLiteral("abc123");
    record.extractorId = QStringLiteral("silence_detect");
    record.extractorVersion = QStringLiteral("1.0.0");
    record.kind = QStringLiteral("silence");
    record.startFrame = 10;
    record.endFrame = 30;
    record.text = QStringLiteral("silence dead air");
    record.confidence = 1.0;
    record.producedUtc = QStringLiteral("2026-08-31T00:00:00.000Z");
    record.metadata = QJsonObject{{QStringLiteral("duration_seconds"), 0.8}};

    VibeCutMediaEvidenceRecord parsed;
    QString error;
    REQUIRE(VibeCutMediaEvidenceRecord::fromJson(record.toJson(), parsed, &error));
    CHECK(error.isEmpty());
    CHECK(parsed.sourceId == record.sourceId);
    CHECK(parsed.extractorId == record.extractorId);
    CHECK(parsed.startFrame == 10);
    CHECK(parsed.endFrame == 30);
    CHECK(parsed.confidence == 1.0);
}

TEST_CASE("media evidence rejects invalid provenance ranges and confidence", "[vibecut][media][evidence]")
{
    QJsonObject valid{{QStringLiteral("source_id"), QStringLiteral("bin:1")},
                      {QStringLiteral("source_fingerprint"), QStringLiteral("fp")},
                      {QStringLiteral("extractor_id"), QStringLiteral("x")},
                      {QStringLiteral("extractor_version"), QStringLiteral("1")},
                      {QStringLiteral("kind"), QStringLiteral("test")},
                      {QStringLiteral("start_frame"), 20},
                      {QStringLiteral("end_frame"), 10},
                      {QStringLiteral("confidence"), 0.5}};
    VibeCutMediaEvidenceRecord record;
    QString error;
    CHECK_FALSE(VibeCutMediaEvidenceRecord::fromJson(valid, record, &error));
    CHECK(error.contains(QStringLiteral("frame range")));

    valid.insert(QStringLiteral("start_frame"), 0);
    valid.insert(QStringLiteral("end_frame"), 10);
    valid.insert(QStringLiteral("confidence"), 1.5);
    error.clear();
    CHECK_FALSE(VibeCutMediaEvidenceRecord::fromJson(valid, record, &error));
    CHECK(error.contains(QStringLiteral("confidence")));

    valid.insert(QStringLiteral("confidence"), -0.5);
    error.clear();
    CHECK_FALSE(VibeCutMediaEvidenceRecord::fromJson(valid, record, &error));
    CHECK(error.contains(QStringLiteral("confidence")));

    valid.insert(QStringLiteral("confidence"), -1.0);
    error.clear();
    CHECK(VibeCutMediaEvidenceRecord::fromJson(valid, record, &error));
    CHECK(error.isEmpty());
    CHECK(record.confidence == -1.0);

    valid.insert(QStringLiteral("confidence"), QStringLiteral("0.5"));
    error.clear();
    CHECK_FALSE(VibeCutMediaEvidenceRecord::fromJson(valid, record, &error));
    CHECK(error.contains(QStringLiteral("numeric")));

    valid.insert(QStringLiteral("confidence"), 0.5);
    valid.insert(QStringLiteral("metadata"), QStringLiteral("not-an-object"));
    error.clear();
    CHECK_FALSE(VibeCutMediaEvidenceRecord::fromJson(valid, record, &error));
    CHECK(error.contains(QStringLiteral("metadata")));

    valid.remove(QStringLiteral("metadata"));
    valid.remove(QStringLiteral("source_fingerprint"));
    error.clear();
    CHECK_FALSE(VibeCutMediaEvidenceRecord::fromJson(valid, record, &error));
    CHECK(error.contains(QStringLiteral("requires")));
}

TEST_CASE("media evidence sidecar loading fails closed", "[vibecut][media][evidence]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("project.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.write("test");
    project.close();
    const QString evidencePath = dir.filePath(VibeCutMediaEvidence::fileName());

    QFile evidence(evidencePath);
    REQUIRE(evidence.open(QIODevice::WriteOnly));
    evidence.write("{not json");
    evidence.close();
    QString error;
    CHECK(VibeCutMediaEvidence::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error).isEmpty());
    CHECK_FALSE(error.isEmpty());

    REQUIRE(evidence.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject unsupported{{QStringLiteral("version"), 999}, {QStringLiteral("records"), QJsonArray{}}};
    evidence.write(QJsonDocument(unsupported).toJson(QJsonDocument::Compact));
    evidence.close();
    error.clear();
    CHECK(VibeCutMediaEvidence::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error).isEmpty());
    CHECK(error.contains(QStringLiteral("Unsupported")));

    VibeCutMediaEvidenceRecord record;
    record.id = QStringLiteral("r1");
    record.sourceId = QStringLiteral("bin:1");
    record.sourceFingerprint = QStringLiteral("fp");
    record.extractorId = QStringLiteral("source_metadata");
    record.extractorVersion = QStringLiteral("1.0.0");
    record.kind = QStringLiteral("source_metadata");
    record.startFrame = 0;
    record.endFrame = 10;
    record.confidence = 1.0;
    const QJsonObject supported{{QStringLiteral("version"), VibeCutMediaEvidence::SchemaVersion},
                                {QStringLiteral("records"), QJsonArray{record.toJson()}}};
    REQUIRE(evidence.open(QIODevice::WriteOnly | QIODevice::Truncate));
    evidence.write(QJsonDocument(supported).toJson(QJsonDocument::Compact));
    evidence.close();
    error.clear();
    const QJsonArray loaded = VibeCutMediaEvidence::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error);
    CHECK(error.isEmpty());
    REQUIRE(loaded.size() == 1);
    CHECK(loaded.first().toObject().value(QStringLiteral("extractor_id")).toString() == QStringLiteral("source_metadata"));
}
