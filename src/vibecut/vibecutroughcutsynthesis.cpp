/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutroughcutsynthesis.h"

#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <limits>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString candidateDedupKey(const VibeCutMediaDocument &document)
{
    return QString::number(document.startFrame) + QLatin1Char(':') + QString::number(document.endFrame) +
           QLatin1Char(':') + document.text.simplified().toLower();
}

int transcriptPriority(const VibeCutMediaDocument &document)
{
    if (document.kind == QLatin1String("transcript_segment")) return 2;
    if (document.kind == QLatin1String("transcript")) return 1;
    return 0;
}

QString sha256Object(const QJsonObject &object)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                                         QCryptographicHash::Sha256).toHex());
}

QString sha256Text(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QJsonObject contextWithoutHash(QJsonObject context)
{
    context.remove(QStringLiteral("context_sha256"));
    context.remove(QStringLiteral("ok")); // tool envelope field is not part of the canonical context identity
    return context;
}

bool validateContextHash(const QJsonObject &context, QString *error)
{
    const QString stored = context.value(QStringLiteral("context_sha256")).toString().trimmed();
    if (stored.size() != 64) {
        if (error) *error = QStringLiteral("Rough-cut context is missing a valid SHA-256 identity.");
        return false;
    }
    const QString computed = sha256Object(contextWithoutHash(context));
    if (stored != computed) {
        if (error) *error = QStringLiteral("Rough-cut context identity does not match its contents.");
        return false;
    }
    return true;
}

QJsonObject contextTool(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("Rough-cut context requires the VibeCut tool surface."));
    const int maxCandidates = input.value(QStringLiteral("max_candidates")).toInt(200);
    const int maxTextChars = input.value(QStringLiteral("max_text_chars")).toInt(600);

    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) return err(indexError);
    QString buildError;
    QJsonObject context = buildVibeCutRoughCutContext(index.documents(), surface->projectRevision(),
                                                      maxCandidates, maxTextChars, &buildError);
    if (!buildError.isEmpty()) return err(buildError);
    context.insert(QStringLiteral("ok"), true);
    return context;
}

QJsonObject validateTool(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("Rough-cut proposal validation requires the VibeCut tool surface."));
    const quint64 currentRevision = surface->projectRevision();
    const quint64 baseRevision = static_cast<quint64>(input.value(QStringLiteral("base_revision")).toDouble(-1));
    if (baseRevision != currentRevision) {
        return err(QStringLiteral("Rough-cut proposal is stale: base revision %1 does not match current revision %2.")
                       .arg(baseRevision).arg(currentRevision));
    }

    const int maxCandidates = input.value(QStringLiteral("context_max_candidates")).toInt(200);
    const int maxTextChars = input.value(QStringLiteral("context_max_text_chars")).toInt(600);
    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) return err(indexError);
    QString contextError;
    const QJsonObject context = buildVibeCutRoughCutContext(index.documents(), currentRevision,
                                                             maxCandidates, maxTextChars, &contextError);
    if (!contextError.isEmpty()) return err(contextError);

    QJsonObject proposal{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("authority"), QStringLiteral("proposal")},
                         {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                         {QStringLiteral("context_sha256"), input.value(QStringLiteral("context_sha256"))},
                         {QStringLiteral("objective"), input.value(QStringLiteral("objective"))},
                         {QStringLiteral("selected_candidate_ids"), input.value(QStringLiteral("selected_candidate_ids"))}};
    if (input.contains(QStringLiteral("max_total_frames"))) {
        proposal.insert(QStringLiteral("max_total_frames"), input.value(QStringLiteral("max_total_frames")));
    }

    QString validateError;
    QJsonObject result = validateVibeCutRoughCutProposal(context, proposal, currentRevision, &validateError);
    if (!validateError.isEmpty()) return err(validateError);
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QJsonObject buildVibeCutRoughCutContext(const QList<VibeCutMediaDocument> &documents,
                                        quint64 baseRevision,
                                        int maxCandidates,
                                        int maxTextChars,
                                        QString *error)
{
    if (error) error->clear();
    if (maxCandidates < 1 || maxCandidates > 300) {
        if (error) *error = QStringLiteral("Rough-cut context max_candidates must be 1..300.");
        return {};
    }
    if (maxTextChars < 64 || maxTextChars > 2048) {
        if (error) *error = QStringLiteral("Rough-cut context max_text_chars must be 64..2048.");
        return {};
    }

    QHash<QString, VibeCutMediaDocument> bestByContent;
    for (const VibeCutMediaDocument &document : documents) {
        const int priority = transcriptPriority(document);
        const QString text = document.text.simplified();
        if (priority == 0 || document.id.trimmed().isEmpty() || text.isEmpty() ||
            document.startFrame < 0 || document.endFrame <= document.startFrame) continue;
        const QString key = candidateDedupKey(document);
        const auto existing = bestByContent.constFind(key);
        if (existing == bestByContent.constEnd() || priority > transcriptPriority(existing.value())) {
            bestByContent.insert(key, document);
        }
    }

    QList<VibeCutMediaDocument> eligible = bestByContent.values();
    std::sort(eligible.begin(), eligible.end(), [](const VibeCutMediaDocument &a, const VibeCutMediaDocument &b) {
        if (a.startFrame != b.startFrame) return a.startFrame < b.startFrame;
        if (a.endFrame != b.endFrame) return a.endFrame < b.endFrame;
        return a.id < b.id;
    });
    if (eligible.isEmpty()) {
        if (error) *error = QStringLiteral("No current transcript/subtitle segments are available for rough-cut synthesis.");
        return {};
    }

    QJsonArray candidates;
    const int emitCount = qMin(maxCandidates, eligible.size());
    for (int i = 0; i < emitCount; ++i) {
        const VibeCutMediaDocument &document = eligible.at(i);
        const QString text = document.text.simplified();
        QJsonObject candidate{{QStringLiteral("candidate_id"), document.id},
                              {QStringLiteral("kind"), document.kind},
                              {QStringLiteral("start_frame"), document.startFrame},
                              {QStringLiteral("end_frame"), document.endFrame},
                              {QStringLiteral("duration_frames"), document.endFrame - document.startFrame},
                              {QStringLiteral("text"), text.left(maxTextChars)},
                              {QStringLiteral("text_sha256"), sha256Text(text)},
                              {QStringLiteral("text_truncated"), text.size() > maxTextChars}};
        const QString sourceId = document.metadata.value(QStringLiteral("source_id")).toString();
        const QString sourceFingerprint = document.metadata.value(QStringLiteral("source_fingerprint")).toString();
        const QString evidenceOrigin = document.metadata.value(QStringLiteral("evidence_origin")).toString();
        const QString extractorId = document.metadata.value(QStringLiteral("extractor_id")).toString();
        const QString extractorVersion = document.metadata.value(QStringLiteral("extractor_version")).toString();
        if (!sourceId.isEmpty()) candidate.insert(QStringLiteral("source_id"), sourceId);
        if (!sourceFingerprint.isEmpty()) candidate.insert(QStringLiteral("source_fingerprint"), sourceFingerprint);
        if (!evidenceOrigin.isEmpty()) candidate.insert(QStringLiteral("evidence_origin"), evidenceOrigin);
        if (!extractorId.isEmpty()) candidate.insert(QStringLiteral("extractor_id"), extractorId);
        if (!extractorVersion.isEmpty()) candidate.insert(QStringLiteral("extractor_version"), extractorVersion);
        if (document.metadata.contains(QStringLiteral("confidence"))) {
            candidate.insert(QStringLiteral("confidence"), document.metadata.value(QStringLiteral("confidence")));
        }
        candidates.append(candidate);
    }

    QJsonObject context{{QStringLiteral("schema_version"), 1},
                        {QStringLiteral("authority"), QStringLiteral("proposal_context")},
                        {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                        {QStringLiteral("max_candidates"), maxCandidates},
                        {QStringLiteral("max_text_chars"), maxTextChars},
                        {QStringLiteral("candidate_count"), candidates.size()},
                        {QStringLiteral("eligible_candidate_count"), eligible.size()},
                        {QStringLiteral("truncated"), eligible.size() > candidates.size()},
                        {QStringLiteral("candidates"), candidates},
                        {QStringLiteral("execution_authority"), QStringLiteral("none")}};
    context.insert(QStringLiteral("context_sha256"), sha256Object(context));
    return context;
}

QJsonObject validateVibeCutRoughCutProposal(const QJsonObject &context,
                                            const QJsonObject &proposal,
                                            quint64 currentRevision,
                                            QString *error)
{
    if (error) error->clear();
    if (context.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        context.value(QStringLiteral("authority")).toString() != QLatin1String("proposal_context") ||
        !context.value(QStringLiteral("candidates")).isArray()) {
        if (error) *error = QStringLiteral("Unsupported or malformed rough-cut context.");
        return {};
    }
    if (!validateContextHash(context, error)) return {};

    const quint64 contextRevision = static_cast<quint64>(context.value(QStringLiteral("base_revision")).toDouble(-1));
    const quint64 proposalRevision = static_cast<quint64>(proposal.value(QStringLiteral("base_revision")).toDouble(-1));
    if (contextRevision != currentRevision || proposalRevision != currentRevision) {
        if (error) *error = QStringLiteral("Rough-cut proposal/context is stale relative to the current project revision.");
        return {};
    }
    if (proposal.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        proposal.value(QStringLiteral("authority")).toString() != QLatin1String("proposal")) {
        if (error) *error = QStringLiteral("Rough-cut proposal must declare schema_version=1 and authority='proposal'.");
        return {};
    }
    if (proposal.value(QStringLiteral("context_sha256")).toString() != context.value(QStringLiteral("context_sha256")).toString()) {
        if (error) *error = QStringLiteral("Rough-cut proposal was not produced from this exact candidate context.");
        return {};
    }
    const QString objective = proposal.value(QStringLiteral("objective")).toString().trimmed();
    if (objective.isEmpty() || objective.size() > 2048) {
        if (error) *error = QStringLiteral("Rough-cut objective must contain 1..2048 characters.");
        return {};
    }
    if (!proposal.value(QStringLiteral("selected_candidate_ids")).isArray()) {
        if (error) *error = QStringLiteral("Rough-cut proposal requires selected_candidate_ids.");
        return {};
    }
    const QJsonArray selected = proposal.value(QStringLiteral("selected_candidate_ids")).toArray();
    if (selected.isEmpty() || selected.size() > 100) {
        if (error) *error = QStringLiteral("Rough-cut proposal must select 1..100 candidate IDs.");
        return {};
    }

    qint64 maxTotalFrames = 0;
    if (proposal.contains(QStringLiteral("max_total_frames"))) {
        const QJsonValue value = proposal.value(QStringLiteral("max_total_frames"));
        if (!value.isDouble() || value.toDouble() < 1.0 || value.toDouble() > static_cast<double>(std::numeric_limits<int>::max())) {
            if (error) *error = QStringLiteral("max_total_frames must be a positive integer when supplied.");
            return {};
        }
        maxTotalFrames = static_cast<qint64>(value.toDouble());
        if (static_cast<double>(maxTotalFrames) != value.toDouble()) {
            if (error) *error = QStringLiteral("max_total_frames must be an integer.");
            return {};
        }
    }

    QHash<QString, QJsonObject> byId;
    for (const QJsonValue &value : context.value(QStringLiteral("candidates")).toArray()) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Rough-cut context contains a malformed candidate.");
            return {};
        }
        const QJsonObject candidate = value.toObject();
        const QString id = candidate.value(QStringLiteral("candidate_id")).toString().trimmed();
        if (id.isEmpty() || byId.contains(id)) {
            if (error) *error = QStringLiteral("Rough-cut context contains an empty or duplicate candidate ID.");
            return {};
        }
        byId.insert(id, candidate);
    }

    QSet<QString> seen;
    QJsonArray resolved;
    qint64 totalFrames = 0;
    bool reordered = false;
    int previousStart = -1;
    int overlapCount = 0;
    QList<QPair<int, int>> selectedRanges;
    for (int order = 0; order < selected.size(); ++order) {
        if (!selected.at(order).isString()) {
            if (error) *error = QStringLiteral("selected_candidate_ids may contain only strings.");
            return {};
        }
        const QString id = selected.at(order).toString().trimmed();
        if (id.isEmpty() || seen.contains(id)) {
            if (error) *error = QStringLiteral("Rough-cut proposal contains an empty or duplicate candidate ID.");
            return {};
        }
        if (!byId.contains(id)) {
            if (error) *error = QStringLiteral("Rough-cut proposal references unknown candidate '%1'.").arg(id);
            return {};
        }
        seen.insert(id);
        QJsonObject candidate = byId.value(id);
        const int start = candidate.value(QStringLiteral("start_frame")).toInt(-1);
        const int end = candidate.value(QStringLiteral("end_frame")).toInt(-1);
        if (start < 0 || end <= start) {
            if (error) *error = QStringLiteral("Rough-cut candidate '%1' has invalid authoritative frame bounds.").arg(id);
            return {};
        }
        if (previousStart >= 0 && start < previousStart) reordered = true;
        previousStart = start;
        for (const auto &range : selectedRanges) {
            if (start < range.second && end > range.first) ++overlapCount;
        }
        selectedRanges.append(qMakePair(start, end));
        totalFrames += static_cast<qint64>(end) - start;
        if (totalFrames > std::numeric_limits<int>::max()) {
            if (error) *error = QStringLiteral("Rough-cut proposal total duration exceeds supported frame bounds.");
            return {};
        }
        candidate.insert(QStringLiteral("proposal_order"), order);
        resolved.append(candidate);
    }
    if (maxTotalFrames > 0 && totalFrames > maxTotalFrames) {
        if (error) *error = QStringLiteral("Rough-cut proposal duration %1 exceeds max_total_frames=%2.")
                               .arg(totalFrames).arg(maxTotalFrames);
        return {};
    }

    QJsonObject identity{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                         {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                         {QStringLiteral("objective"), objective},
                         {QStringLiteral("selected_candidate_ids"), selected}};
    if (maxTotalFrames > 0) identity.insert(QStringLiteral("max_total_frames"), maxTotalFrames);

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("proposal")},
                       {QStringLiteral("proposal_id"), sha256Object(identity)},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("objective"), objective},
                       {QStringLiteral("segment_count"), resolved.size()},
                       {QStringLiteral("total_frames"), totalFrames},
                       {QStringLiteral("reorders_timeline"), reordered},
                       {QStringLiteral("overlap_warning_count"), overlapCount},
                       {QStringLiteral("segments"), resolved},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")},
                       {QStringLiteral("next_step"), QStringLiteral("review_then_translate_to_governed_edit_plan")}};
}

bool registerVibeCutRoughCutSynthesisTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject contextInput{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{
                                       {QStringLiteral("max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                                     {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
                                       {QStringLiteral("max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                                      {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}}}},
                                   {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy contextPolicy;
    contextPolicy.name = QStringLiteral("rough_cut_context");
    contextPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), contextPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Build a bounded revision-bound rough-cut candidate context from current canonical transcript/subtitle documents. Candidates expose stable IDs and authoritative ranges/provenance only; full normalized transcript text is SHA-256-bound even when the displayed preview is truncated. This tool grants no edit authority.")},
                                          {QStringLiteral("input_schema"), contextInput}},
                              contextPolicy, [surfacePtr](const QJsonObject &input) { return contextTool(surfacePtr, input); }, error)) return false;

    const QJsonObject proposalInput{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{
                                        {QStringLiteral("base_revision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                        {QStringLiteral("context_sha256"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64}}},
                                        {QStringLiteral("context_max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
                                        {QStringLiteral("context_max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}},
                                        {QStringLiteral("objective"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 2048}}},
                                        {QStringLiteral("selected_candidate_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                                                {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 100},
                                                                                                {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                        {QStringLiteral("max_total_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}}},
                                    {QStringLiteral("required"), QJsonArray{QStringLiteral("base_revision"), QStringLiteral("context_sha256"),
                                                                            QStringLiteral("context_max_candidates"), QStringLiteral("context_max_text_chars"),
                                                                            QStringLiteral("objective"), QStringLiteral("selected_candidate_ids")}},
                                    {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy proposalPolicy;
    proposalPolicy.name = QStringLiteral("rough_cut_proposal_validate");
    proposalPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), proposalPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Validate an ordered rough-cut proposal against the exact current revision-bound candidate context. The proposer may supply candidate IDs and editorial objective only; authoritative ranges/provenance are resolved from context. Output remains non-executable proposal authority.")},
                                            {QStringLiteral("input_schema"), proposalInput}},
                                proposalPolicy, [surfacePtr](const QJsonObject &input) { return validateTool(surfacePtr, input); }, error);
}
