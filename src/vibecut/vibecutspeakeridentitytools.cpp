/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutspeakeridentitytools.h"

#include "vibecutmediaevidence.h"
#include "vibecutspeakeridentity.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool clusterFromEvidenceObject(const QJsonObject &record, VibeCutSpeakerClusterKey &cluster, QString *error)
{
    if (error) error->clear();
    if (record.value(QStringLiteral("kind")).toString() != QLatin1String("speaker_segment")) {
        if (error) *error = QStringLiteral("Evidence record is not a diarization speaker_segment.");
        return false;
    }
    cluster.sourceId = record.value(QStringLiteral("source_id")).toString().trimmed();
    cluster.sourceFingerprint = record.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    cluster.extractorId = record.value(QStringLiteral("extractor_id")).toString().trimmed();
    cluster.extractorVersion = record.value(QStringLiteral("extractor_version")).toString().trimmed();
    cluster.speakerClusterId = record.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("speaker_cluster_id")).toString().trimmed();
    if (!cluster.valid()) {
        if (error) *error = QStringLiteral("Speaker evidence does not contain a complete source/extractor/cluster identity key.");
        return false;
    }
    return true;
}

bool findSpeakerEvidence(const QString &evidenceId, QJsonObject &record, VibeCutSpeakerClusterKey &cluster, QString *error)
{
    if (error) error->clear();
    const QString id = evidenceId.trimmed();
    if (id.isEmpty()) {
        if (error) *error = QStringLiteral("evidence_id must not be empty.");
        return false;
    }
    QString loadError;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&loadError);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    for (const QJsonValue &value : records) {
        const QJsonObject candidate = value.toObject();
        if (candidate.value(QStringLiteral("id")).toString() != id) continue;
        if (!clusterFromEvidenceObject(candidate, cluster, error)) return false;
        record = candidate;
        return true;
    }
    if (error) *error = QStringLiteral("Unknown current media-evidence id: %1").arg(id);
    return false;
}

QJsonObject identityList(const QJsonObject &)
{
    QString error;
    const QJsonObject root = VibeCutSpeakerIdentityStore::loadCurrent(&error);
    if (!error.isEmpty()) return err(error);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("schema_version"), VibeCutSpeakerIdentityStore::SchemaVersion},
                       {QStringLiteral("entity_count"), root.value(QStringLiteral("entities")).toArray().size()},
                       {QStringLiteral("association_count"), root.value(QStringLiteral("associations")).toArray().size()},
                       {QStringLiteral("entities"), root.value(QStringLiteral("entities"))},
                       {QStringLiteral("associations"), root.value(QStringLiteral("associations"))}};
}

QJsonObject speakerSegmentsList(const QJsonObject &input)
{
    QString evidenceError;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&evidenceError);
    if (!evidenceError.isEmpty()) return err(evidenceError);
    QString identityError;
    const QJsonObject identities = VibeCutSpeakerIdentityStore::loadCurrent(&identityError);
    if (!identityError.isEmpty()) return err(identityError);

    const QString sourceId = input.value(QStringLiteral("source_id")).toString().trimmed();
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(500), 5000);
    QJsonArray result;
    for (const QJsonValue &value : records) {
        const QJsonObject record = value.toObject();
        if (record.value(QStringLiteral("kind")).toString() != QLatin1String("speaker_segment")) continue;
        if (!sourceId.isEmpty() && record.value(QStringLiteral("source_id")).toString() != sourceId) continue;
        VibeCutSpeakerClusterKey cluster;
        QString clusterError;
        if (!clusterFromEvidenceObject(record, cluster, &clusterError)) continue;
        const QJsonObject entity = VibeCutSpeakerIdentityStore::resolve(identities, cluster);
        QJsonObject resolved = record;
        resolved.insert(QStringLiteral("speaker_cluster_key"), cluster.stableKey());
        resolved.insert(QStringLiteral("identity_status"), entity.isEmpty() ? QStringLiteral("unassigned") : QStringLiteral("assigned"));
        resolved.insert(QStringLiteral("speaker_entity_id"), entity.value(QStringLiteral("id")).toString());
        resolved.insert(QStringLiteral("speaker_display_name"), entity.value(QStringLiteral("display_name")).toString());
        result.append(resolved);
        if (result.size() >= limit) break;
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("record_count"), result.size()},
                       {QStringLiteral("source_id_filter"), sourceId},
                       {QStringLiteral("records"), result}};
}

QJsonObject upsertEntity(const QJsonObject &input)
{
    const QString displayName = input.value(QStringLiteral("display_name")).toString();
    const QString requestedId = input.value(QStringLiteral("entity_id")).toString();
    QString resolvedId;
    QString error;
    if (!VibeCutSpeakerIdentityStore::upsertEntityCurrent(requestedId, displayName, &resolvedId, &error)) return err(error);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("entity_id"), resolvedId},
                       {QStringLiteral("display_name"), displayName.trimmed()},
                       {QStringLiteral("user_governed"), true}};
}

QJsonObject assignCluster(const QJsonObject &input)
{
    QJsonObject evidence;
    VibeCutSpeakerClusterKey cluster;
    QString error;
    if (!findSpeakerEvidence(input.value(QStringLiteral("evidence_id")).toString(), evidence, cluster, &error)) return err(error);
    const QString entityId = input.value(QStringLiteral("entity_id")).toString().trimmed();
    if (!VibeCutSpeakerIdentityStore::assignClusterCurrent(cluster, entityId, &error)) return err(error);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("evidence_id"), evidence.value(QStringLiteral("id"))},
                       {QStringLiteral("entity_id"), entityId},
                       {QStringLiteral("cluster"), cluster.toJson()},
                       {QStringLiteral("user_governed"), true}};
}

QJsonObject unassignCluster(const QJsonObject &input)
{
    QJsonObject evidence;
    VibeCutSpeakerClusterKey cluster;
    QString error;
    if (!findSpeakerEvidence(input.value(QStringLiteral("evidence_id")).toString(), evidence, cluster, &error)) return err(error);
    if (!VibeCutSpeakerIdentityStore::unassignClusterCurrent(cluster, &error)) return err(error);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("evidence_id"), evidence.value(QStringLiteral("id"))},
                       {QStringLiteral("cluster"), cluster.toJson()},
                       {QStringLiteral("unassigned"), true},
                       {QStringLiteral("user_governed"), true}};
}

QJsonObject noArgs()
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), QJsonObject{}},
                       {QStringLiteral("additionalProperties"), false}};
}

VibeCutToolPolicy identityWritePolicy(const QString &name)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.confirmationRequired = true;
    policy.mutatesProject = false;
    return policy;
}
}

bool registerVibeCutSpeakerIdentityTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolPolicy identityListPolicy;
    identityListPolicy.name = QStringLiteral("speaker_identity_list");
    identityListPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), identityListPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("List user-governed speaker entities and exact diarization-cluster associations stored in the project-local speaker identity ledger. Extractors never write this ledger.")},
                                          {QStringLiteral("input_schema"), noArgs()}},
                              identityListPolicy, identityList, error)) return false;

    const QJsonObject segmentInput{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{
                                       {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                       {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 5000}}}}},
                                   {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy segmentPolicy;
    segmentPolicy.name = QStringLiteral("speaker_segments_list");
    segmentPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), segmentPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("List persisted diarization speaker segments and resolve only exact user-confirmed identity associations. Unassigned clusters remain anonymous; changed source fingerprints or extractor versions do not inherit old names.")},
                                          {QStringLiteral("input_schema"), segmentInput}},
                              segmentPolicy, speakerSegmentsList, error)) return false;

    const QJsonObject entityInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("entity_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                                 {QStringLiteral("description"), QStringLiteral("Existing entity id to rename; omit to create a new speaker entity.")}}},
                                      {QStringLiteral("display_name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 256}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("display_name")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const VibeCutToolPolicy entityPolicy = identityWritePolicy(QStringLiteral("speaker_entity_upsert"));
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), entityPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Create or explicitly rename a project-local speaker entity. Always requires confirmation, even in high-trust modes; diarization providers cannot call this persistence path.")},
                                          {QStringLiteral("input_schema"), entityInput}},
                              entityPolicy, upsertEntity, error)) return false;

    const QJsonObject assignInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("evidence_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                                   {QStringLiteral("description"), QStringLiteral("Current persisted speaker_segment evidence id whose anonymous cluster is being identified.")}}},
                                      {QStringLiteral("entity_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("evidence_id"), QStringLiteral("entity_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const VibeCutToolPolicy assignPolicy = identityWritePolicy(QStringLiteral("speaker_cluster_assign"));
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), assignPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Explicitly associate the anonymous cluster behind a current persisted speaker_segment evidence record with a user-governed speaker entity. The association is bound to source fingerprint, extractor id/version and cluster id, and always requires confirmation.")},
                                          {QStringLiteral("input_schema"), assignInput}},
                              assignPolicy, assignCluster, error)) return false;

    const QJsonObject unassignInput{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{{QStringLiteral("evidence_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                    {QStringLiteral("required"), QJsonArray{QStringLiteral("evidence_id")}},
                                    {QStringLiteral("additionalProperties"), false}};
    const VibeCutToolPolicy unassignPolicy = identityWritePolicy(QStringLiteral("speaker_cluster_unassign"));
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), unassignPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Remove an explicit user-governed identity association from the anonymous cluster behind a current speaker_segment evidence record. Always requires confirmation.")},
                                            {QStringLiteral("input_schema"), unassignInput}},
                                unassignPolicy, unassignCluster, error);
}
