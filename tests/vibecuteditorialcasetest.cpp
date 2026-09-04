/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuteditorialcase.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject manifest()
{
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("case_id"), QStringLiteral("interview-001")},
                       {QStringLiteral("task_type"), QStringLiteral("rough_cut")},
                       {QStringLiteral("objective"), QStringLiteral("Build the clearest concise explanation")},
                       {QStringLiteral("context_sha256"), QString(64, QLatin1Char('a'))},
                       {QStringLiteral("candidates"), QJsonArray{
                           QJsonObject{{QStringLiteral("candidate_id"), QStringLiteral("candidate-a")},
                                       {QStringLiteral("display_label"), QStringLiteral("Option-A")},
                                       {QStringLiteral("proposal_id"), QString(64, QLatin1Char('b'))}},
                           QJsonObject{{QStringLiteral("candidate_id"), QStringLiteral("candidate-b")},
                                       {QStringLiteral("display_label"), QStringLiteral("Option-B")},
                                       {QStringLiteral("proposal_id"), QString(64, QLatin1Char('c'))}}}},
                       {QStringLiteral("reference"), QJsonObject{
                           {QStringLiteral("source"), QStringLiteral("golden")},
                           {QStringLiteral("reference_id"), QStringLiteral("editor-reference-v1")},
                           {QStringLiteral("expected_candidate_ids"), QJsonArray{QStringLiteral("seg-1"), QStringLiteral("seg-3")}}}}};
}

QJsonObject schemaByName(const VibeCutToolSurface &surface, const QString &name)
{
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == name) return schema;
    }
    return {};
}
}

TEST_CASE("editorial case normalizes frozen proposal candidates and explicit reference", "[vibecut][editorial-case]")
{
    QString error;
    const QJsonObject result = validateVibeCutEditorialCase(manifest(), &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("evaluation_case"));
    CHECK(result.value(QStringLiteral("candidate_count")).toInt() == 2);
    CHECK(result.value(QStringLiteral("case_sha256")).toString().size() == 64);
    CHECK(result.value(QStringLiteral("blind_candidate_labels_required")).toBool(false));
    CHECK_FALSE(result.value(QStringLiteral("quality_ground_truth")).toBool(true));
    CHECK(result.value(QStringLiteral("execution_authority")).toString() == QStringLiteral("none"));
    const QJsonObject reference = result.value(QStringLiteral("reference")).toObject();
    CHECK(reference.value(QStringLiteral("source")).toString() == QStringLiteral("golden"));
    CHECK_FALSE(reference.value(QStringLiteral("ground_truth_claim")).toBool(true));
}

TEST_CASE("editorial case identity is deterministic and changes with proposal identity", "[vibecut][editorial-case][identity]")
{
    QString error;
    const QJsonObject first = validateVibeCutEditorialCase(manifest(), &error);
    REQUIRE(error.isEmpty());
    const QJsonObject second = validateVibeCutEditorialCase(manifest(), &error);
    REQUIRE(error.isEmpty());
    CHECK(first.value(QStringLiteral("case_sha256")) == second.value(QStringLiteral("case_sha256")));

    QJsonObject changed = manifest();
    QJsonArray candidates = changed.value(QStringLiteral("candidates")).toArray();
    QJsonObject candidate = candidates.at(1).toObject();
    candidate.insert(QStringLiteral("proposal_id"), QString(64, QLatin1Char('d')));
    candidates.replace(1, candidate);
    changed.insert(QStringLiteral("candidates"), candidates);
    const QJsonObject third = validateVibeCutEditorialCase(changed, &error);
    REQUIRE(error.isEmpty());
    CHECK(first.value(QStringLiteral("case_sha256")) != third.value(QStringLiteral("case_sha256")));
}

TEST_CASE("editorial case rejects duplicate labels proposal ids and invalid reference provenance", "[vibecut][editorial-case][integrity]")
{
    QString error;
    QJsonObject duplicateLabel = manifest();
    QJsonArray candidates = duplicateLabel.value(QStringLiteral("candidates")).toArray();
    QJsonObject second = candidates.at(1).toObject();
    second.insert(QStringLiteral("display_label"), QStringLiteral("Option-A"));
    candidates.replace(1, second);
    duplicateLabel.insert(QStringLiteral("candidates"), candidates);
    CHECK(validateVibeCutEditorialCase(duplicateLabel, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unique"), Qt::CaseInsensitive));

    error.clear();
    QJsonObject duplicateProposal = manifest();
    candidates = duplicateProposal.value(QStringLiteral("candidates")).toArray();
    second = candidates.at(1).toObject();
    second.insert(QStringLiteral("proposal_id"), candidates.at(0).toObject().value(QStringLiteral("proposal_id")));
    candidates.replace(1, second);
    duplicateProposal.insert(QStringLiteral("candidates"), candidates);
    CHECK(validateVibeCutEditorialCase(duplicateProposal, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unique"), Qt::CaseInsensitive));

    error.clear();
    QJsonObject badReference = manifest();
    QJsonObject reference = badReference.value(QStringLiteral("reference")).toObject();
    reference.insert(QStringLiteral("source"), QStringLiteral("model_generated"));
    badReference.insert(QStringLiteral("reference"), reference);
    CHECK(validateVibeCutEditorialCase(badReference, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("golden"), Qt::CaseInsensitive));
}

TEST_CASE("editorial case tool is read only and excludes provider identity and execution controls", "[vibecut][editorial-case][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("editorial_case_validate")));
    CHECK(policies.value(QStringLiteral("editorial_case_validate")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("editorial_case_validate")).mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("editorial_case_validate"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject manifestSchema = schema.value(QStringLiteral("input_schema")).toObject()
                                           .value(QStringLiteral("properties")).toObject()
                                           .value(QStringLiteral("manifest")).toObject();
    const QJsonObject properties = manifestSchema.value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("candidates")));
    CHECK_FALSE(properties.contains(QStringLiteral("provider")));
    CHECK_FALSE(properties.contains(QStringLiteral("model")));
    CHECK_FALSE(properties.contains(QStringLiteral("auto_execute")));
    CHECK_FALSE(properties.contains(QStringLiteral("pass_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
}
