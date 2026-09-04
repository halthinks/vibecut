/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutnarrative.h"

#include "vibecutembeddingstore.h"
#include "vibecutmediaindex.h"
#include "vibecutroughcutsynthesis.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
const QString kMiniLmModel = QStringLiteral("sentence-transformers/all-MiniLM-L6-v2");
const QString kMiniLmRevision = QStringLiteral("1110a243fdf4706b3f48f1d95db1a4f5529b4d41");
constexpr int kMiniLmDimension = 384;

struct SequenceItem {
    QString id;
    QString text;
    int start = -1;
    int end = -1;
    QSet<QString> tokens;
    QVector<double> semantic;
};

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QSet<QString> tokens(const QString &text)
{
    QSet<QString> result;
    const QStringList parts = text.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.size() >= 2) result.insert(part);
    }
    return result;
}

double jaccard(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() && b.isEmpty()) return 1.0;
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    int intersection = 0;
    for (const QString &token : a) if (b.contains(token)) ++intersection;
    const int unionCount = a.size() + b.size() - intersection;
    return unionCount > 0 ? static_cast<double>(intersection) / unionCount : 0.0;
}

double dot(const QVector<double> &a, const QVector<double> &b)
{
    if (a.size() != b.size() || a.isEmpty()) return -2.0;
    long double value = 0.0L;
    for (int i = 0; i < a.size(); ++i) value += static_cast<long double>(a.at(i)) * b.at(i);
    return qBound(-1.0, static_cast<double>(value), 1.0);
}

bool parseUnitVector(const QJsonValue &value, QVector<double> &vector)
{
    vector.clear();
    if (!value.isArray()) return false;
    const QJsonArray raw = value.toArray();
    if (raw.size() != kMiniLmDimension) return false;
    long double normSquared = 0.0L;
    vector.reserve(raw.size());
    for (const QJsonValue &entry : raw) {
        if (!entry.isDouble() || !std::isfinite(entry.toDouble())) {
            vector.clear();
            return false;
        }
        const double number = entry.toDouble();
        vector.append(number);
        normSquared += static_cast<long double>(number) * number;
    }
    const double norm = std::sqrt(static_cast<double>(normSquared));
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 0.001) {
        vector.clear();
        return false;
    }
    return true;
}

bool exactInteger(const QJsonObject &input, const QString &key, qint64 minimum, qint64 maximum,
                  qint64 defaultValue, bool required, qint64 &value, QString *error)
{
    if (!input.contains(key)) {
        if (required) {
            if (error) *error = QStringLiteral("%1 is required.").arg(key);
            return false;
        }
        value = defaultValue;
        return true;
    }
    const QJsonValue raw = input.value(key);
    if (!raw.isDouble()) {
        if (error) *error = QStringLiteral("%1 must be an integer.").arg(key);
        return false;
    }
    const double number = raw.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        if (error) *error = QStringLiteral("%1 must be an integer in the supported range %2..%3.")
                               .arg(key).arg(minimum).arg(maximum);
        return false;
    }
    value = static_cast<qint64>(number);
    return true;
}

bool buildCurrentContext(VibeCutToolSurface *surface, int maxCandidates, int maxTextChars,
                         QJsonObject &context, QString *error)
{
    if (error) error->clear();
    if (!surface) {
        if (error) *error = QStringLiteral("Narrative analysis requires the VibeCut tool surface.");
        return false;
    }
    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) {
        if (error) *error = indexError;
        return false;
    }
    context = buildVibeCutRoughCutContext(index.documents(), surface->projectRevision(), maxCandidates, maxTextChars, error);
    return !context.isEmpty() && (!error || error->isEmpty());
}

QJsonObject currentSemanticVectors(const QJsonObject &context, QString *error)
{
    if (error) error->clear();
    QString storeError;
    const QJsonObject root = VibeCutEmbeddingStore::loadCurrent(&storeError);
    if (!storeError.isEmpty()) {
        if (error) *error = storeError;
        return {};
    }
    QHash<QString, QJsonObject> candidates;
    for (const QJsonValue &value : context.value(QStringLiteral("candidates")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject candidate = value.toObject();
        const QString id = candidate.value(QStringLiteral("candidate_id")).toString().trimmed();
        if (!id.isEmpty()) candidates.insert(id, candidate);
    }
    QJsonObject vectors;
    for (const QJsonValue &value : root.value(QStringLiteral("records")).toArray()) {
        if (!value.isObject()) continue;
        VibeCutEmbeddingRecord record;
        QString recordError;
        if (!VibeCutEmbeddingRecord::fromJson(value.toObject(), record, &recordError)) {
            if (error) *error = recordError;
            return {};
        }
        if (record.model != kMiniLmModel || record.modelRevision != kMiniLmRevision ||
            record.modality != QLatin1String("text") || !candidates.contains(record.anchorId)) continue;
        const QJsonObject candidate = candidates.value(record.anchorId);
        const QString candidateSource = candidate.value(QStringLiteral("source_id")).toString();
        const QString candidateFingerprint = candidate.value(QStringLiteral("source_fingerprint")).toString();
        const QString candidateTextHash = candidate.value(QStringLiteral("text_sha256")).toString();
        if (!candidateSource.isEmpty() && record.sourceId != candidateSource) continue;
        if (!candidateFingerprint.isEmpty() && record.sourceFingerprint != candidateFingerprint) continue;
        if (!candidateTextHash.isEmpty() && record.metadata.value(QStringLiteral("text_sha256")).toString() != candidateTextHash) continue;
        QJsonArray raw;
        for (double number : record.vector) raw.append(number);
        vectors.insert(record.anchorId, raw);
    }
    return vectors;
}

QJsonObject tool(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("Narrative analysis requires the VibeCut tool surface."));
    QString numberError;
    qint64 baseRevision = -1, maxCandidates = 200, maxTextChars = 600, maxBoundaries = 5, maxRepetitions = 10;
    constexpr qint64 MaxExactJsonInteger = 9007199254740991LL;
    if (!exactInteger(input, QStringLiteral("base_revision"), 0, MaxExactJsonInteger, -1, true, baseRevision, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_candidates"), 1, 300, 200, true, maxCandidates, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_text_chars"), 64, 2048, 600, true, maxTextChars, &numberError) ||
        !exactInteger(input, QStringLiteral("max_boundary_candidates"), 0, 25, 5, false, maxBoundaries, &numberError) ||
        !exactInteger(input, QStringLiteral("max_repetition_candidates"), 0, 50, 10, false, maxRepetitions, &numberError)) {
        return err(numberError);
    }
    if (static_cast<quint64>(baseRevision) != surface->projectRevision()) return err(QStringLiteral("Narrative context is stale."));
    if (!input.value(QStringLiteral("selected_candidate_ids")).isArray()) return err(QStringLiteral("selected_candidate_ids must be an array."));
    const QString contextSha = input.value(QStringLiteral("context_sha256")).toString().trimmed();
    if (contextSha.size() != 64) return err(QStringLiteral("context_sha256 must contain exactly 64 characters."));

    QJsonObject context;
    QString contextError;
    if (!buildCurrentContext(surface, static_cast<int>(maxCandidates), static_cast<int>(maxTextChars), context, &contextError)) return err(contextError);
    if (context.value(QStringLiteral("context_sha256")).toString() != contextSha) return err(QStringLiteral("Narrative analysis was not requested against the exact current context."));

    QString embeddingError;
    const QJsonObject vectors = currentSemanticVectors(context, &embeddingError);
    if (!embeddingError.isEmpty()) return err(embeddingError);
    QString analysisError;
    QJsonObject result = analyzeVibeCutNarrativeSequence(context, input.value(QStringLiteral("selected_candidate_ids")).toArray(),
                                                         vectors, surface->projectRevision(), static_cast<int>(maxBoundaries),
                                                         static_cast<int>(maxRepetitions), &analysisError);
    if (!analysisError.isEmpty()) return err(analysisError);
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QJsonObject analyzeVibeCutNarrativeSequence(const QJsonObject &context,
                                            const QJsonArray &selectedCandidateIds,
                                            const QJsonObject &semanticVectorsByCandidate,
                                            quint64 currentRevision,
                                            int maxBoundaryCandidates,
                                            int maxRepetitionCandidates,
                                            QString *error)
{
    if (error) error->clear();
    if (maxBoundaryCandidates < 0 || maxBoundaryCandidates > 25 || maxRepetitionCandidates < 0 || maxRepetitionCandidates > 50) {
        if (error) *error = QStringLiteral("Narrative candidate limits are outside supported bounds.");
        return {};
    }
    const QJsonObject proposal{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("authority"), QStringLiteral("proposal")},
                               {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                               {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                               {QStringLiteral("objective"), QStringLiteral("Analyze narrative continuity of selected rough-cut candidates")},
                               {QStringLiteral("selected_candidate_ids"), selectedCandidateIds}};
    QString proposalError;
    const QJsonObject validated = validateVibeCutRoughCutProposal(context, proposal, currentRevision, &proposalError);
    if (!proposalError.isEmpty()) {
        if (error) *error = proposalError;
        return {};
    }

    QList<SequenceItem> items;
    for (const QJsonValue &value : validated.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject segment = value.toObject();
        SequenceItem item;
        item.id = segment.value(QStringLiteral("candidate_id")).toString();
        item.text = segment.value(QStringLiteral("text")).toString();
        item.start = segment.value(QStringLiteral("start_frame")).toInt(-1);
        item.end = segment.value(QStringLiteral("end_frame")).toInt(-1);
        item.tokens = tokens(item.text);
        parseUnitVector(semanticVectorsByCandidate.value(item.id), item.semantic);
        items.append(item);
    }
    if (items.isEmpty()) {
        if (error) *error = QStringLiteral("Narrative analysis has no validated sequence items.");
        return {};
    }

    QJsonArray progression;
    QJsonArray adjacency;
    QList<QJsonObject> boundaries;
    int semanticAdjacencyCount = 0;
    for (int i = 0; i < items.size(); ++i) {
        progression.append(QJsonObject{{QStringLiteral("order"), i},
                                       {QStringLiteral("candidate_id"), items.at(i).id},
                                       {QStringLiteral("start_frame"), items.at(i).start},
                                       {QStringLiteral("end_frame"), items.at(i).end},
                                       {QStringLiteral("text"), items.at(i).text},
                                       {QStringLiteral("semantic_vector_available"), !items.at(i).semantic.isEmpty()}});
        if (i == 0) continue;
        const SequenceItem &previous = items.at(i - 1);
        const SequenceItem &current = items.at(i);
        const double lexical = jaccard(previous.tokens, current.tokens);
        const double semanticRaw = dot(previous.semantic, current.semantic);
        const bool semanticAvailable = semanticRaw >= -1.0;
        if (semanticAvailable) ++semanticAdjacencyCount;
        const double semanticUnit = semanticAvailable ? qBound(0.0, (semanticRaw + 1.0) / 2.0, 1.0) : -1.0;
        const double continuity = semanticAvailable ? semanticUnit : lexical;
        QJsonObject edge{{QStringLiteral("from_candidate_id"), previous.id},
                         {QStringLiteral("to_candidate_id"), current.id},
                         {QStringLiteral("edge_index"), i - 1},
                         {QStringLiteral("lexical_jaccard"), lexical},
                         {QStringLiteral("semantic_available"), semanticAvailable},
                         {QStringLiteral("semantic_cosine"), semanticAvailable ? semanticRaw : -2.0},
                         {QStringLiteral("continuity_component_used"), semanticAvailable ? QStringLiteral("minilm_cosine_rescaled") : QStringLiteral("lexical_jaccard")},
                         {QStringLiteral("continuity_score"), continuity},
                         {QStringLiteral("score_semantics"), QStringLiteral("relative_adjacency_similarity_not_narrative_quality_probability")}};
        adjacency.append(edge);
        boundaries.append(edge);
    }
    std::sort(boundaries.begin(), boundaries.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double aScore = a.value(QStringLiteral("continuity_score")).toDouble();
        const double bScore = b.value(QStringLiteral("continuity_score")).toDouble();
        if (aScore != bScore) return aScore < bScore;
        return a.value(QStringLiteral("edge_index")).toInt() < b.value(QStringLiteral("edge_index")).toInt();
    });
    while (boundaries.size() > maxBoundaryCandidates) boundaries.removeLast();
    QJsonArray boundaryCandidates;
    for (int i = 0; i < boundaries.size(); ++i) {
        QJsonObject candidate = boundaries.at(i);
        candidate.insert(QStringLiteral("relative_rank"), i + 1);
        candidate.insert(QStringLiteral("candidate_semantics"), QStringLiteral("lowest_relative_adjacent_similarity_possible_section_boundary_not_fact"));
        boundaryCandidates.append(candidate);
    }

    QList<QJsonObject> repetitions;
    for (int i = 0; i < items.size(); ++i) {
        for (int j = i + 2; j < items.size(); ++j) {
            const double lexical = jaccard(items.at(i).tokens, items.at(j).tokens);
            const double semanticRaw = dot(items.at(i).semantic, items.at(j).semantic);
            const bool semanticAvailable = semanticRaw >= -1.0;
            const double semanticUnit = semanticAvailable ? qBound(0.0, (semanticRaw + 1.0) / 2.0, 1.0) : -1.0;
            const double similarity = semanticAvailable ? semanticUnit : lexical;
            repetitions.append(QJsonObject{{QStringLiteral("first_candidate_id"), items.at(i).id},
                                           {QStringLiteral("second_candidate_id"), items.at(j).id},
                                           {QStringLiteral("order_distance"), j - i},
                                           {QStringLiteral("lexical_jaccard"), lexical},
                                           {QStringLiteral("semantic_available"), semanticAvailable},
                                           {QStringLiteral("semantic_cosine"), semanticAvailable ? semanticRaw : -2.0},
                                           {QStringLiteral("similarity_component_used"), semanticAvailable ? QStringLiteral("minilm_cosine_rescaled") : QStringLiteral("lexical_jaccard")},
                                           {QStringLiteral("repetition_similarity_score"), similarity}});
        }
    }
    std::sort(repetitions.begin(), repetitions.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double aScore = a.value(QStringLiteral("repetition_similarity_score")).toDouble();
        const double bScore = b.value(QStringLiteral("repetition_similarity_score")).toDouble();
        if (aScore != bScore) return aScore > bScore;
        const int aDistance = a.value(QStringLiteral("order_distance")).toInt();
        const int bDistance = b.value(QStringLiteral("order_distance")).toInt();
        if (aDistance != bDistance) return aDistance > bDistance;
        return a.value(QStringLiteral("first_candidate_id")).toString() < b.value(QStringLiteral("first_candidate_id")).toString();
    });
    while (repetitions.size() > maxRepetitionCandidates) repetitions.removeLast();
    QJsonArray repetitionCandidates;
    for (int i = 0; i < repetitions.size(); ++i) {
        QJsonObject candidate = repetitions.at(i);
        candidate.insert(QStringLiteral("relative_rank"), i + 1);
        candidate.insert(QStringLiteral("candidate_semantics"), QStringLiteral("highest_relative_nonadjacent_similarity_possible_repetition_not_fact"));
        repetitionCandidates.append(candidate);
    }

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("derived_analysis")},
                       {QStringLiteral("analysis_semantics"), QStringLiteral("relative_semantic_lexical_narrative_structure_candidates_not_story_truth")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("segment_count"), items.size()},
                       {QStringLiteral("adjacency_edge_count"), qMax(0, items.size() - 1)},
                       {QStringLiteral("semantic_adjacency_coverage"), items.size() > 1 ? static_cast<double>(semanticAdjacencyCount) / (items.size() - 1) : 0.0},
                       {QStringLiteral("topic_progression"), progression},
                       {QStringLiteral("adjacency_continuity"), adjacency},
                       {QStringLiteral("section_boundary_candidates"), boundaryCandidates},
                       {QStringLiteral("repetition_candidates"), repetitionCandidates},
                       {QStringLiteral("normative_thresholds_applied"), false},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")}};
}

bool registerVibeCutNarrativeTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("base_revision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                {QStringLiteral("context_sha256"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64}}},
                                {QStringLiteral("context_max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
                                {QStringLiteral("context_max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}},
                                {QStringLiteral("selected_candidate_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                                                {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 100},
                                                                                                {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                {QStringLiteral("max_boundary_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 25}}},
                                {QStringLiteral("max_repetition_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 50}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("base_revision"), QStringLiteral("context_sha256"),
                                                                    QStringLiteral("context_max_candidates"), QStringLiteral("context_max_text_chars"),
                                                                    QStringLiteral("selected_candidate_ids")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("rough_cut_narrative_analyze");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Analyze one exact current rough-cut candidate sequence for relative adjacent continuity, possible section boundaries and possible non-adjacent repetition. Uses exact current MiniLM vectors when provenance/text hashes match, otherwise lexical similarity. Rankings are relative candidates, not narrative facts or quality probabilities.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [surfacePtr](const QJsonObject &args) { return tool(surfacePtr, args); }, error);
}
