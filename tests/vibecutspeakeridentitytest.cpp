/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutspeakeridentity.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

TEST_CASE("speaker identity associations are exact to source fingerprint extractor version and cluster", "[vibecut][speaker-identity][diarization]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("project.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();
    const QUrl projectUrl = QUrl::fromLocalFile(projectPath);

    QString entityId;
    QString error;
    REQUIRE(VibeCutSpeakerIdentityStore::upsertEntityForProjectUrl(projectUrl, QString(), QStringLiteral("Alice"), &entityId, &error));
    REQUIRE_FALSE(entityId.isEmpty());
    CHECK(error.isEmpty());

    VibeCutSpeakerClusterKey cluster;
    cluster.sourceId = QStringLiteral("bin:7");
    cluster.sourceFingerprint = QStringLiteral("fingerprint-a");
    cluster.extractorId = QStringLiteral("local_diarizer");
    cluster.extractorVersion = QStringLiteral("1.0.0");
    cluster.speakerClusterId = QStringLiteral("SPEAKER_00");
    REQUIRE(cluster.valid());
    REQUIRE(VibeCutSpeakerIdentityStore::assignClusterForProjectUrl(projectUrl, cluster, entityId, &error));

    const QJsonObject root = VibeCutSpeakerIdentityStore::loadForProjectUrl(projectUrl, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject resolved = VibeCutSpeakerIdentityStore::resolve(root, cluster);
    REQUIRE_FALSE(resolved.isEmpty());
    CHECK(resolved.value(QStringLiteral("id")).toString() == entityId);
    CHECK(resolved.value(QStringLiteral("display_name")).toString() == QStringLiteral("Alice"));

    VibeCutSpeakerClusterKey changedSource = cluster;
    changedSource.sourceFingerprint = QStringLiteral("fingerprint-b");
    CHECK(VibeCutSpeakerIdentityStore::resolve(root, changedSource).isEmpty());

    VibeCutSpeakerClusterKey changedExtractor = cluster;
    changedExtractor.extractorVersion = QStringLiteral("2.0.0");
    CHECK(VibeCutSpeakerIdentityStore::resolve(root, changedExtractor).isEmpty());

    VibeCutSpeakerClusterKey changedCluster = cluster;
    changedCluster.speakerClusterId = QStringLiteral("SPEAKER_01");
    CHECK(VibeCutSpeakerIdentityStore::resolve(root, changedCluster).isEmpty());

    REQUIRE(VibeCutSpeakerIdentityStore::unassignClusterForProjectUrl(projectUrl, cluster, &error));
    const QJsonObject after = VibeCutSpeakerIdentityStore::loadForProjectUrl(projectUrl, &error);
    REQUIRE(error.isEmpty());
    CHECK(VibeCutSpeakerIdentityStore::resolve(after, cluster).isEmpty());
}

TEST_CASE("speaker identity sidecar fails closed when cluster key integrity is tampered", "[vibecut][speaker-identity][integrity]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("tampered.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();
    const QUrl projectUrl = QUrl::fromLocalFile(projectPath);

    QString entityId;
    QString error;
    REQUIRE(VibeCutSpeakerIdentityStore::upsertEntityForProjectUrl(projectUrl, QString(), QStringLiteral("Alice"), &entityId, &error));

    VibeCutSpeakerClusterKey cluster;
    cluster.sourceId = QStringLiteral("bin:9");
    cluster.sourceFingerprint = QStringLiteral("fingerprint-original");
    cluster.extractorId = QStringLiteral("local_pyannote");
    cluster.extractorVersion = QStringLiteral("community-1/4.0.7");
    cluster.speakerClusterId = QStringLiteral("SPEAKER_00");
    REQUIRE(VibeCutSpeakerIdentityStore::assignClusterForProjectUrl(projectUrl, cluster, entityId, &error));

    const QString sidecarPath = dir.filePath(VibeCutSpeakerIdentityStore::fileName());
    QFile sidecar(sidecarPath);
    REQUIRE(sidecar.open(QIODevice::ReadOnly));
    QJsonDocument document = QJsonDocument::fromJson(sidecar.readAll());
    sidecar.close();
    REQUIRE(document.isObject());

    QJsonObject root = document.object();
    QJsonArray associations = root.value(QStringLiteral("associations")).toArray();
    REQUIRE(associations.size() == 1);
    QJsonObject association = associations.at(0).toObject();
    association.insert(QStringLiteral("source_fingerprint"), QStringLiteral("fingerprint-tampered"));
    // Deliberately leave cluster_key untouched: a hand-edited sidecar must not
    // be able to reuse a user-approved identity on different evidence.
    associations[0] = association;
    root.insert(QStringLiteral("associations"), associations);

    REQUIRE(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(sidecar.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) > 0);
    sidecar.close();

    error.clear();
    CHECK(VibeCutSpeakerIdentityStore::loadForProjectUrl(projectUrl, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("cluster_key"), Qt::CaseInsensitive));

    // resolve() is independently fail-closed even when handed an in-memory
    // object that bypassed the loader.
    CHECK(VibeCutSpeakerIdentityStore::resolve(root, cluster).isEmpty());
}

TEST_CASE("speaker naming and cluster assignment always require explicit confirmation", "[vibecut][speaker-identity][policy]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QHash<QString, VibeCutToolPolicy> policies = surface.policies();

    REQUIRE(policies.contains(QStringLiteral("speaker_identity_list")));
    REQUIRE(policies.contains(QStringLiteral("speaker_segments_list")));
    REQUIRE(policies.contains(QStringLiteral("speaker_entity_upsert")));
    REQUIRE(policies.contains(QStringLiteral("speaker_cluster_assign")));
    REQUIRE(policies.contains(QStringLiteral("speaker_cluster_unassign")));

    CHECK(policies.value(QStringLiteral("speaker_identity_list")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("speaker_segments_list")).risk == VibeCutToolRisk::ReadOnly);

    for (const QString &name : {QStringLiteral("speaker_entity_upsert"),
                                QStringLiteral("speaker_cluster_assign"),
                                QStringLiteral("speaker_cluster_unassign")}) {
        const VibeCutToolPolicy policy = policies.value(name);
        CHECK(policy.risk == VibeCutToolRisk::ExternalSideEffect);
        CHECK(policy.confirmationRequired);
        CHECK(policy.requiresConfirmation(VibeCutTrustMode::Turbo));
        CHECK_FALSE(policy.mutatesProject);
    }
}
