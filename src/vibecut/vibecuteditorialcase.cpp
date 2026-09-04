/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteditorialcase.h"

#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
bool boundedString(const QJsonObject &object, const QString &key, int maxLength, QString &value, QString *error)
{
    if (!object.value(key).isString()) {
        if (error) *error = QStringLiteral("%1 must be a string.").arg(key);
        return false;
    }
    value = object.value(key).toString().trimmed();
    if (value.isEmpty() || value.size() > maxLength) {
        if (error) *error = QStringLiteral("%1 must contain 1..%2 characters.").arg(key).arg(maxLength);
        return false;
    }
    return true;
}

bool sha256String(const QJsonValue &raw, const QString &label, QString &value, QString *error)
{
    if (!raw.isString()) {
        if (error) *error = QStringLiteral("%1 must be a SHA-256 hex string.").arg(label);
        return false;
    }
    value = raw.toString().trimmed().toLower();
    if (value.size() != 64) {
        if (error) *error = QStringLiteral("%1 must contain exactly 64 hexadecimal characters.").arg(label);
        return false;
    }
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f'))) {
            if (error) *error = QStringLiteral("%1 must contain hexadecimal characters only.").arg(label);
            return false;
        }
    }
    return true;
}

bool supportedTask(const QString &task)
{
    return task == QLatin1String("rough_cut") || task == QLatin1String("highlight") || task == QLatin1String("broll");
}

bool parseReferenceIds(const QJsonArray &array, QJsonArray &normalized, QString *error)
{
    if (array.size() > 100) {
        if (error) *error = QStringLiteral("reference.expected_candidate_ids exceeds the 100-item bound.");
        return false;
    }
    QSet<QString> seen;
    for (const QJsonValue &value : array) {
        if (!value.isString()) {
            if (error) *error = QStringLiteral("reference.expected_candidate_ids may contain strings only.");
            return false;
        }
        const QString id = value.toString().trimmed();
        if (id.isEmpty() || id.size() > 1024 || seen.contains(id)) {
            if (error) *error = QStringLiteral("reference expected candidate IDs must be unique non-empty strings up to 1024 characters.");
            return false;
        }
        seen.insert(id);
        normalized.append(id);
    }
    return true;
}

QJsonObject toolHandler(const QJsonObject &input)
{
    if (!input.value(QStringLiteral("manifest")).isObject()) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("manifest must be an object.")}};
    }
    QString error;
    QJsonObject result = validateVibeCutEditorialCase(input.value(QStringLiteral("manifest")).toObject(), &error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QJsonObject validateVibeCutEditorialCase(const QJsonObject &manifest, QString *error)
{
    if (error) error->clear();
    if (manifest.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
        if (error) *error = QStringLiteral("Editorial evaluation case schema_version must be 1.");
        return {};
    }

    QString caseId;
    QString taskType;
    QString objective;
    QString contextSha;
    if (!boundedString(manifest, QStringLiteral("case_id"), 128, caseId, error) ||
        !boundedString(manifest, QStringLiteral("task_type"), 32, taskType, error) ||
        !boundedString(manifest, QStringLiteral("objective"), 2048, objective, error) ||
        !sha256String(manifest.value(QStringLiteral("context_sha256")), QStringLiteral("context_sha256"), contextSha, error)) return {};
    if (!supportedTask(taskType)) {
        if (error) *error = QStringLiteral("task_type must be rough_cut, highlight, or broll.");
        return {};
    }
    if (!manifest.value(QStringLiteral("candidates")).isArray()) {
        if (error) *error = QStringLiteral("Editorial evaluation case requires a candidates array.");
        return {};
    }
    const QJsonArray candidates = manifest.value(QStringLiteral("candidates")).toArray();
    if (candidates.size() < 2 || candidates.size() > 5) {
        if (error) *error = QStringLiteral("Editorial evaluation case requires 2..5 blinded candidate proposals.");
        return {};
    }

    QSet<QString> candidateIds;
    QSet<QString> labels;
    QSet<QString> proposalIds;
    QJsonArray normalizedCandidates;
    for (const QJsonValue &value : candidates) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Each editorial evaluation candidate must be an object.");
            return {};
        }
        const QJsonObject candidate = value.toObject();
        QString candidateId;
        QString displayLabel;
        QString proposalId;
        if (!boundedString(candidate, QStringLiteral("candidate_id"), 128, candidateId, error) ||
            !boundedString(candidate, QStringLiteral("display_label"), 32, displayLabel, error) ||
            !sha256String(candidate.value(QStringLiteral("proposal_id")), QStringLiteral("candidate.proposal_id"), proposalId, error)) return {};
        if (candidateIds.contains(candidateId) || labels.contains(displayLabel) || proposalIds.contains(proposalId)) {
            if (error) *error = QStringLiteral("Candidate IDs, blinded display labels and proposal IDs must each be unique within a case.");
            return {};
        }
        if (displayLabel.compare(candidateId, Qt::CaseInsensitive) == 0 || displayLabel.compare(proposalId, Qt::CaseInsensitive) == 0) {
            if (error) *error = QStringLiteral("display_label must be an opaque presentation label distinct from candidate_id and proposal_id.");
            return {};
        }
        candidateIds.insert(candidateId);
        labels.insert(displayLabel);
        proposalIds.insert(proposalId);
        normalizedCandidates.append(QJsonObject{{QStringLiteral("candidate_id"), candidateId},
                                                {QStringLiteral("display_label"), displayLabel},
                                                {QStringLiteral("proposal_id"), proposalId}});
    }

    QJsonObject normalized{{QStringLiteral("schema_version"), 1},
                           {QStringLiteral("authority"), QStringLiteral("evaluation_case")},
                           {QStringLiteral("case_id"), caseId},
                           {QStringLiteral("task_type"), taskType},
                           {QStringLiteral("objective"), objective},
                           {QStringLiteral("context_sha256"), contextSha},
                           {QStringLiteral("candidates"), normalizedCandidates},
                           {QStringLiteral("candidate_count"), normalizedCandidates.size()},
                           {QStringLiteral("blind_candidate_labels_required"), true},
                           {QStringLiteral("quality_ground_truth"), false},
                           {QStringLiteral("execution_authority"), QStringLiteral("none")}};

    if (manifest.contains(QStringLiteral("reference"))) {
        if (!manifest.value(QStringLiteral("reference")).isObject()) {
            if (error) *error = QStringLiteral("reference must be an object when supplied.");
            return {};
        }
        const QJsonObject reference = manifest.value(QStringLiteral("reference")).toObject();
        QString source;
        QString referenceId;
        if (!boundedString(reference, QStringLiteral("source"), 32, source, error) ||
            !boundedString(reference, QStringLiteral("reference_id"), 128, referenceId, error)) return {};
        if (source != QLatin1String("golden") && source != QLatin1String("human_consensus")) {
            if (error) *error = QStringLiteral("reference.source must be golden or human_consensus.");
            return {};
        }
        if (!reference.value(QStringLiteral("expected_candidate_ids")).isArray()) {
            if (error) *error = QStringLiteral("reference.expected_candidate_ids must be an array.");
            return {};
        }
        QJsonArray expectedIds;
        if (!parseReferenceIds(reference.value(QStringLiteral("expected_candidate_ids")).toArray(), expectedIds, error)) return {};
        QJsonObject normalizedReference{{QStringLiteral("source"), source},
                                        {QStringLiteral("reference_id"), referenceId},
                                        {QStringLiteral("expected_candidate_ids"), expectedIds},
                                        {QStringLiteral("ground_truth_claim"), false}};
        if (reference.contains(QStringLiteral("notes"))) {
            if (!reference.value(QStringLiteral("notes")).isString() || reference.value(QStringLiteral("notes")).toString().size() > 1000) {
                if (error) *error = QStringLiteral("reference.notes must be a string of at most 1000 characters.");
                return {};
            }
            normalizedReference.insert(QStringLiteral("notes"), reference.value(QStringLiteral("notes")).toString().trimmed());
        }
        normalized.insert(QStringLiteral("reference"), normalizedReference);
    }

    const QByteArray canonical = QJsonDocument(normalized).toJson(QJsonDocument::Compact);
    normalized.insert(QStringLiteral("case_sha256"), QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex()));
    return normalized;
}

bool registerVibeCutEditorialCaseTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject shaSchema{{QStringLiteral("type"), QStringLiteral("string")},
                                {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64},
                                {QStringLiteral("pattern"), QStringLiteral("^[0-9A-Fa-f]{64}$")}};
    const QJsonObject candidateSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                      {QStringLiteral("properties"), QJsonObject{
                                          {QStringLiteral("candidate_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 128}}},
                                          {QStringLiteral("display_label"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 32}}},
                                          {QStringLiteral("proposal_id"), shaSchema}}},
                                      {QStringLiteral("required"), QJsonArray{QStringLiteral("candidate_id"), QStringLiteral("display_label"), QStringLiteral("proposal_id")}},
                                      {QStringLiteral("additionalProperties"), false}};
    const QJsonObject referenceSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                      {QStringLiteral("properties"), QJsonObject{
                                          {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("golden"), QStringLiteral("human_consensus")}}}},
                                          {QStringLiteral("reference_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 128}}},
                                          {QStringLiteral("expected_candidate_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("maxItems"), 100}, {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 1024}}}}},
                                          {QStringLiteral("notes"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 1000}}}}},
                                      {QStringLiteral("required"), QJsonArray{QStringLiteral("source"), QStringLiteral("reference_id"), QStringLiteral("expected_candidate_ids")}},
                                      {QStringLiteral("additionalProperties"), false}};
    const QJsonObject manifestSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"), QJsonObject{
                                         {QStringLiteral("schema_version"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("const"), 1}}},
                                         {QStringLiteral("case_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 128}}},
                                         {QStringLiteral("task_type"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("rough_cut"), QStringLiteral("highlight"), QStringLiteral("broll")}}}},
                                         {QStringLiteral("objective"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 2048}}},
                                         {QStringLiteral("context_sha256"), shaSchema},
                                         {QStringLiteral("candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 2}, {QStringLiteral("maxItems"), 5}, {QStringLiteral("items"), candidateSchema}}},
                                         {QStringLiteral("reference"), referenceSchema}}},
                                     {QStringLiteral("required"), QJsonArray{QStringLiteral("schema_version"), QStringLiteral("case_id"), QStringLiteral("task_type"), QStringLiteral("objective"), QStringLiteral("context_sha256"), QStringLiteral("candidates")}},
                                     {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("editorial_case_validate");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Validate and normalize one frozen blinded editorial-evaluation case manifest: exact context hash, 2..5 opaque candidate labels bound to proposal hashes, and an optional explicitly sourced structural reference. Grants no quality ground truth or execution authority.")},
                                            {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                         {QStringLiteral("properties"), QJsonObject{{QStringLiteral("manifest"), manifestSchema}}},
                                                                                         {QStringLiteral("required"), QJsonArray{QStringLiteral("manifest")}},
                                                                                         {QStringLiteral("additionalProperties"), false}}}},
                                policy, toolHandler, error);
}
