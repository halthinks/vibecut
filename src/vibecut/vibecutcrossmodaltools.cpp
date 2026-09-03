/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutcrossmodaltools.h"

#include "kdenlivesettings.h"
#include "vibecutembeddingstore.h"
#include "vibecutextractorrequest.h"
#include "vibecutjobmanager.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"
#include "vibecutvisionruntime.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>

#include <cmath>

namespace {
const QString kModel = QStringLiteral("google/siglip-base-patch16-224");
const QString kModelRevision = QStringLiteral("7fd15f0689c79d79e38b1c2e2e2370a7bf2761ed");
const QString kProducerPrefix = QStringLiteral("semantic_visual_siglip:");
const QString kProducerVersion = QStringLiteral("1.0.0");
const QString kTransformersVersion = QStringLiteral("5.16.1");
const QString kTorchVersion = QStringLiteral("2.14.0");
constexpr int kDimension = 768;
constexpr int kMaxSamples = 500;

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString helperPath()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                  QStringLiteral("scripts/vibecut/semantic_siglip.py"));
}

QString stateName(VibeCutJobState state)
{
    switch (state) {
    case VibeCutJobState::Queued: return QStringLiteral("queued");
    case VibeCutJobState::Running: return QStringLiteral("running");
    case VibeCutJobState::CancelRequested: return QStringLiteral("cancel_requested");
    case VibeCutJobState::Succeeded: return QStringLiteral("succeeded");
    case VibeCutJobState::Failed: return QStringLiteral("failed");
    case VibeCutJobState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

bool runtimeReady(QString *error)
{
    if (!vibeCutVisionDependenciesReady(error)) return false;
    const QString helper = helperPath();
    if (helper.isEmpty() || !QFileInfo::exists(helper)) {
        if (error) *error = QStringLiteral("VibeCut's installed SigLIP semantic helper was not found.");
        return false;
    }
    return true;
}

void configureProcess(QProcess *process, VibeCutJobManager *jobs, const QString &jobId)
{
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"), QStringLiteral("1"));
    environment.insert(QStringLiteral("DO_NOT_TRACK"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process,
                     [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->terminate();
    });
}

void writeRequestWhenStarted(QProcess *process, const QByteArray &payload)
{
    QObject::connect(process, &QProcess::started, process, [process, payload]() {
        if (process->write(payload) != payload.size()) {
            process->kill();
            return;
        }
        process->closeWriteChannel();
    });
}

void bindFailedStart(QProcess *process, VibeCutJobManager *jobs, const QString &jobId)
{
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, jobs, jobId](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && !job.terminal()) jobs->markFailed(jobId, QStringLiteral("Could not launch the SigLIP semantic helper."));
        process->deleteLater();
    });
}

bool cancelled(VibeCutJobManager *jobs, const QString &jobId, const QString &message)
{
    VibeCutJob job;
    if (!jobs->job(jobId, job) || job.state != VibeCutJobState::CancelRequested) return false;
    jobs->markCancelled(jobId, message);
    return true;
}

bool parseUnitVector(const QJsonValue &value, QVector<double> &unit, QString *error)
{
    if (!value.isArray()) {
        if (error) *error = QStringLiteral("SigLIP helper returned a non-array embedding.");
        return false;
    }
    const QJsonArray raw = value.toArray();
    if (raw.size() != kDimension) {
        if (error) *error = QStringLiteral("SigLIP helper returned dimension %1, expected %2.").arg(raw.size()).arg(kDimension);
        return false;
    }
    QVector<double> original;
    original.reserve(raw.size());
    for (const QJsonValue &entry : raw) {
        if (!entry.isDouble() || !std::isfinite(entry.toDouble())) {
            if (error) *error = QStringLiteral("SigLIP helper returned a non-numeric or non-finite vector value.");
            return false;
        }
        original.append(entry.toDouble());
    }
    QString normalizeError;
    if (!VibeCutEmbeddingStore::normalizeVector(original, unit, &normalizeError)) {
        if (error) *error = normalizeError;
        return false;
    }
    long double dot = 0.0L;
    for (int i = 0; i < original.size(); ++i) dot += static_cast<long double>(original.at(i)) * unit.at(i);
    if (std::abs(static_cast<double>(dot) - 1.0) > 0.001) {
        if (error) *error = QStringLiteral("SigLIP helper output was not unit-normalized as declared.");
        return false;
    }
    return true;
}

bool validateHelperRoot(const QJsonObject &root, const QString &operation, int expectedCount, QString *error)
{
    if (error) error->clear();
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        root.value(QStringLiteral("operation")).toString() != operation ||
        root.value(QStringLiteral("authority")).toString() != QLatin1String("model_representation") ||
        root.value(QStringLiteral("score_semantics")).toString() != QLatin1String("cosine_similarity_same_embedding_space") ||
        root.value(QStringLiteral("model")).toString() != kModel ||
        root.value(QStringLiteral("model_revision")).toString() != kModelRevision ||
        root.value(QStringLiteral("model_license")).toString() != QLatin1String("Apache-2.0") ||
        root.value(QStringLiteral("dimension")).toInt(-1) != kDimension ||
        !root.value(QStringLiteral("unit_normalized")).toBool(false) ||
        root.value(QStringLiteral("transformers_version")).toString() != kTransformersVersion ||
        !root.value(QStringLiteral("torch_version")).toString().startsWith(kTorchVersion) ||
        root.value(QStringLiteral("item_count")).toInt(-1) != expectedCount ||
        !root.value(QStringLiteral("items")).isArray() || root.value(QStringLiteral("items")).toArray().size() != expectedCount) {
        if (error) *error = QStringLiteral("SigLIP helper returned an unsupported or provenance-mismatched result schema.");
        return false;
    }
    return true;
}

QJsonObject status(const QJsonObject &)
{
    QString readyError;
    const bool ready = runtimeReady(&readyError);
    QString storeError;
    const QJsonObject root = VibeCutEmbeddingStore::loadCurrent(&storeError);
    int visualCount = 0;
    if (storeError.isEmpty()) {
        for (const QJsonValue &value : root.value(QStringLiteral("records")).toArray()) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("model")).toString() == kModel &&
                object.value(QStringLiteral("model_revision")).toString() == kModelRevision &&
                object.value(QStringLiteral("modality")).toString() == QLatin1String("visual")) ++visualCount;
        }
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("model"), kModel},
                       {QStringLiteral("model_revision"), kModelRevision},
                       {QStringLiteral("model_license"), QStringLiteral("Apache-2.0")},
                       {QStringLiteral("dimension"), kDimension},
                       {QStringLiteral("score_semantics"), QStringLiteral("cosine_similarity_same_embedding_space")},
                       {QStringLiteral("runtime"), QStringLiteral("shared_vision")},
                       {QStringLiteral("helper"), helperPath()},
                       {QStringLiteral("dependencies_ready"), ready},
                       {QStringLiteral("dependency_error"), ready ? QString() : readyError},
                       {QStringLiteral("visual_embedding_count"), visualCount},
                       {QStringLiteral("embedding_store_error"), storeError},
                       {QStringLiteral("ready"), ready},
                       {QStringLiteral("note"), QStringLiteral("SigLIP text/image vectors share one pinned embedding space. Cosine similarity is cross-modal ranking evidence, not probability or observed semantic fact.")}};
}

QJsonObject startVisualRefresh(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("SigLIP visual refresh requires the VibeCut runtime."));
    QString readyError;
    if (!runtimeReady(&readyError)) return err(readyError);

    QJsonObject normalized;
    QString normalizeError;
    if (!normalizeVibeCutExtractorRequest(QStringLiteral("embeddings"), input, normalized, &normalizeError)) return err(normalizeError);
    if (!normalized.value(QStringLiteral("has_video")).toBool(false)) return err(QStringLiteral("SigLIP visual embedding requires a source with video."));
    const QString sourcePath = normalized.value(QStringLiteral("source_path")).toString();
    const QString sourceId = normalized.value(QStringLiteral("source_id")).toString();
    const QString sourceFingerprint = normalized.value(QStringLiteral("source_fingerprint")).toString();
    const QString binId = normalized.value(QStringLiteral("bin_id")).toString();
    const int startFrame = normalized.value(QStringLiteral("start_frame")).toInt(-1);
    const int endFrame = normalized.value(QStringLiteral("end_frame")).toInt(-1);
    if (sourcePath.isEmpty() || sourceId.isEmpty() || sourceFingerprint.isEmpty() || startFrame < 0 || endFrame <= startFrame) {
        return err(QStringLiteral("Normalized SigLIP source request is incomplete."));
    }

    const QJsonObject parameters = normalized.value(QStringLiteral("parameters")).toObject();
    const int interval = parameters.value(QStringLiteral("sample_interval_frames")).toInt(30);
    const int maxSamples = parameters.value(QStringLiteral("max_samples")).toInt(300);
    const int batchSize = parameters.value(QStringLiteral("batch_size")).toInt(16);
    const QString device = parameters.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
    if (interval < 1 || interval > 1000000 || maxSamples < 1 || maxSamples > kMaxSamples || batchSize < 1 || batchSize > 128) {
        return err(QStringLiteral("SigLIP sampling requires sample_interval_frames=1..1000000, max_samples=1..500 and batch_size=1..128."));
    }
    if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) return err(QStringLiteral("device must be auto, cpu, or cuda."));
    const qint64 required = 1 + (static_cast<qint64>(endFrame) - startFrame - 1) / interval;
    if (required < 1 || required > maxSamples) {
        return err(QStringLiteral("SigLIP range/cadence requires %1 samples, exceeding max_samples=%2; increase sample_interval_frames or narrow the source range.").arg(required).arg(maxSamples));
    }
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) return err(QStringLiteral("Kdenlive has no valid configured FFmpeg executable for SigLIP frame sampling."));

    QJsonArray requestItems;
    QHash<QString, int> frameById;
    for (qint64 i = 0; i < required; ++i) {
        const qint64 frame64 = static_cast<qint64>(startFrame) + i * interval;
        if (frame64 < startFrame || frame64 >= endFrame || frame64 > std::numeric_limits<int>::max()) return err(QStringLiteral("SigLIP sample-frame calculation overflowed authoritative bounds."));
        const int frame = static_cast<int>(frame64);
        const QString id = QStringLiteral("%1:frame:%2").arg(sourceId).arg(frame);
        requestItems.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("frame"), frame}});
        frameById.insert(id, frame);
    }

    const QJsonObject request{{QStringLiteral("schema_version"), 1},
                              {QStringLiteral("operation"), QStringLiteral("images")},
                              {QStringLiteral("model"), kModel},
                              {QStringLiteral("model_revision"), kModelRevision},
                              {QStringLiteral("device"), device},
                              {QStringLiteral("batch_size"), batchSize},
                              {QStringLiteral("source"), sourcePath},
                              {QStringLiteral("ffmpeg"), ffmpeg},
                              {QStringLiteral("items"), requestItems}};
    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (payload.size() > 8 * 1024 * 1024) return err(QStringLiteral("SigLIP visual request exceeds the 8 MiB helper limit."));

    VibeCutJobManager *jobs = tools->jobManager();
    const quint64 baseRevision = surface->projectRevision();
    const QString jobId = jobs->createJob(QStringLiteral("semantic_visual_refresh"), QStringLiteral("SigLIP visual embeddings · %1").arg(sourceId), true);
    jobs->markRunning(jobId, QStringLiteral("Embedding authoritative sampled frames with SigLIP…"));
    auto *process = new QProcess(jobs);
    configureProcess(process, jobs, jobId);
    writeRequestWhenStarted(process, payload);
    bindFailedStart(process, jobs, jobId);
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId, surface, baseRevision, sourceId, sourceFingerprint, binId,
                      startFrame, endFrame, interval, required, frameById]
                     (int exitCode, QProcess::ExitStatus processStatus) {
        if (cancelled(jobs, jobId, QStringLiteral("SigLIP visual refresh cancelled."))) {
            process->deleteLater();
            return;
        }
        if (surface->projectRevision() != baseRevision) {
            jobs->markFailed(jobId, QStringLiteral("Project revision changed while SigLIP visual embeddings were running; refusing stale vector promotion."));
            process->deleteLater();
            return;
        }
        if (processStatus != QProcess::NormalExit || exitCode != 0) {
            const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(8000).trimmed();
            jobs->markFailed(jobId, stderrText.isEmpty() ? QStringLiteral("SigLIP visual helper failed with code %1.").arg(exitCode) : stderrText);
            process->deleteLater();
            return;
        }
        const QByteArray stdoutData = process->readAllStandardOutput();
        if (stdoutData.size() > 32 * 1024 * 1024) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP visual output exceeded the 32 MiB safety limit."));
            process->deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument response = QJsonDocument::fromJson(stdoutData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP visual helper returned malformed JSON: %1").arg(parseError.errorString()));
            process->deleteLater();
            return;
        }
        const QJsonObject root = response.object();
        QString schemaError;
        if (!validateHelperRoot(root, QStringLiteral("images"), static_cast<int>(required), &schemaError)) {
            jobs->markFailed(jobId, schemaError);
            process->deleteLater();
            return;
        }
        QSet<QString> seen;
        QList<VibeCutEmbeddingRecord> records;
        for (const QJsonValue &value : root.value(QStringLiteral("items")).toArray()) {
            if (!value.isObject()) {
                jobs->markFailed(jobId, QStringLiteral("SigLIP helper returned a non-object image embedding."));
                process->deleteLater();
                return;
            }
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!frameById.contains(id) || seen.contains(id)) {
                jobs->markFailed(jobId, QStringLiteral("SigLIP helper returned an unknown or duplicate visual anchor id."));
                process->deleteLater();
                return;
            }
            seen.insert(id);
            QVector<double> vector;
            QString vectorError;
            if (!parseUnitVector(item.value(QStringLiteral("vector")), vector, &vectorError)) {
                jobs->markFailed(jobId, vectorError);
                process->deleteLater();
                return;
            }
            const int frame = frameById.value(id);
            VibeCutEmbeddingRecord record;
            record.anchorKind = QStringLiteral("visual_frame");
            record.anchorId = id;
            record.sourceId = sourceId;
            record.sourceFingerprint = sourceFingerprint;
            record.modality = QStringLiteral("visual");
            record.model = kModel;
            record.modelRevision = kModelRevision;
            record.producerId = kProducerPrefix + sourceId;
            record.producerVersion = kProducerVersion;
            record.startFrame = frame;
            record.endFrame = frame + 1;
            record.vector = vector;
            record.metadata = QJsonObject{{QStringLiteral("authority"), QStringLiteral("model_representation")},
                                          {QStringLiteral("score_semantics"), QStringLiteral("cosine_similarity_same_embedding_space")},
                                          {QStringLiteral("sample_frame"), frame},
                                          {QStringLiteral("sample_interval_frames"), interval},
                                          {QStringLiteral("bin_id"), binId}};
            records.append(record);
        }
        if (seen.size() != required) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP helper did not return every requested visual anchor."));
            process->deleteLater();
            return;
        }
        const QJsonObject result{{QStringLiteral("kind"), QStringLiteral("semantic_visual_refresh")},
                                 {QStringLiteral("source_id"), sourceId},
                                 {QStringLiteral("source_fingerprint"), sourceFingerprint},
                                 {QStringLiteral("record_count"), records.size()},
                                 {QStringLiteral("model"), kModel},
                                 {QStringLiteral("model_revision"), kModelRevision},
                                 {QStringLiteral("dimension"), kDimension},
                                 {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)}};
        QString resultError;
        if (!jobs->setResult(jobId, result, &resultError)) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP refresh result could not be recorded before persistence: %1").arg(resultError));
            process->deleteLater();
            return;
        }
        const QString producerId = kProducerPrefix + sourceId;
        QString persistError;
        if (!VibeCutEmbeddingStore::replaceProducerModelCurrent(producerId, kProducerVersion, kModel, kModelRevision, records, &persistError)) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP visual embedding promotion failed: %1").arg(persistError));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Persisted %1 current SigLIP visual embedding(s) for %2.").arg(records.size()).arg(sourceId));
        process->deleteLater();
    });
    process->start(vibeCutVisionPython(), {helperPath()});
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), jobId}, {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("source_fingerprint"), sourceFingerprint},
                       {QStringLiteral("sample_count"), static_cast<int>(required)},
                       {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)}};
}

QJsonObject startCrossModalSearch(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("SigLIP search requires the VibeCut runtime."));
    QString readyError;
    if (!runtimeReady(&readyError)) return err(readyError);
    const QString query = input.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty() || query.size() > 2048) return err(QStringLiteral("query must contain 1..2048 characters."));
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(25), 100);
    const double minSimilarity = qBound(-1.0, input.value(QStringLiteral("min_similarity")).toDouble(-1.0), 1.0);
    const QString device = input.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
    if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) return err(QStringLiteral("device must be auto, cpu, or cuda."));

    QString storeError;
    const QJsonObject stored = VibeCutEmbeddingStore::loadCurrent(&storeError);
    if (!storeError.isEmpty()) return err(storeError);
    QHash<QString, QString> currentFingerprintBySource;
    QSet<QString> checkedSources;
    QJsonArray currentRecords;
    int staleOrUnavailable = 0;
    for (const QJsonValue &value : stored.value(QStringLiteral("records")).toArray()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("model")).toString() != kModel ||
            object.value(QStringLiteral("model_revision")).toString() != kModelRevision ||
            object.value(QStringLiteral("modality")).toString() != QLatin1String("visual")) continue;
        const QString sourceId = object.value(QStringLiteral("source_id")).toString();
        if (!sourceId.startsWith(QStringLiteral("bin:"))) {
            ++staleOrUnavailable;
            continue;
        }
        if (!checkedSources.contains(sourceId)) {
            checkedSources.insert(sourceId);
            QJsonObject normalized;
            QString normalizeError;
            const QString binId = sourceId.mid(4);
            if (normalizeVibeCutExtractorRequest(QStringLiteral("embeddings"), QJsonObject{{QStringLiteral("bin_id"), binId}}, normalized, &normalizeError)) {
                currentFingerprintBySource.insert(sourceId, normalized.value(QStringLiteral("source_fingerprint")).toString());
            }
        }
        if (currentFingerprintBySource.value(sourceId).isEmpty() ||
            object.value(QStringLiteral("source_fingerprint")).toString() != currentFingerprintBySource.value(sourceId)) {
            ++staleOrUnavailable;
            continue;
        }
        currentRecords.append(object);
    }
    if (currentRecords.isEmpty()) return err(QStringLiteral("No current SigLIP visual embeddings are available. Run semantic_visual_refresh for video bin assets first."));
    const QJsonObject currentRoot{{QStringLiteral("version"), VibeCutEmbeddingStore::SchemaVersion},
                                  {QStringLiteral("records"), currentRecords}};

    const QJsonObject request{{QStringLiteral("schema_version"), 1},
                              {QStringLiteral("operation"), QStringLiteral("query")},
                              {QStringLiteral("model"), kModel},
                              {QStringLiteral("model_revision"), kModelRevision},
                              {QStringLiteral("device"), device},
                              {QStringLiteral("batch_size"), 1},
                              {QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("query")},
                                                                              {QStringLiteral("text"), query}}}}}};
    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    VibeCutJobManager *jobs = tools->jobManager();
    const quint64 baseRevision = surface->projectRevision();
    const QString jobId = jobs->createJob(QStringLiteral("semantic_crossmodal_search"), QStringLiteral("Visual semantic search · %1").arg(query.left(80)), true);
    jobs->markRunning(jobId, QStringLiteral("Encoding text query in the SigLIP image/text space…"));
    auto *process = new QProcess(jobs);
    configureProcess(process, jobs, jobId);
    writeRequestWhenStarted(process, payload);
    bindFailedStart(process, jobs, jobId);
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId, surface, baseRevision, query, limit, minSimilarity, currentRoot, staleOrUnavailable]
                     (int exitCode, QProcess::ExitStatus processStatus) {
        if (cancelled(jobs, jobId, QStringLiteral("SigLIP cross-modal search cancelled."))) {
            process->deleteLater();
            return;
        }
        if (surface->projectRevision() != baseRevision) {
            jobs->markFailed(jobId, QStringLiteral("Project revision changed while SigLIP search was running; refusing stale ranked results."));
            process->deleteLater();
            return;
        }
        if (processStatus != QProcess::NormalExit || exitCode != 0) {
            const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(8000).trimmed();
            jobs->markFailed(jobId, stderrText.isEmpty() ? QStringLiteral("SigLIP query helper failed with code %1.").arg(exitCode) : stderrText);
            process->deleteLater();
            return;
        }
        const QByteArray stdoutData = process->readAllStandardOutput();
        if (stdoutData.size() > 4 * 1024 * 1024) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP query output exceeded the 4 MiB safety limit."));
            process->deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument response = QJsonDocument::fromJson(stdoutData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP query helper returned malformed JSON: %1").arg(parseError.errorString()));
            process->deleteLater();
            return;
        }
        const QJsonObject root = response.object();
        QString schemaError;
        if (!validateHelperRoot(root, QStringLiteral("query"), 1, &schemaError)) {
            jobs->markFailed(jobId, schemaError);
            process->deleteLater();
            return;
        }
        const QJsonObject item = root.value(QStringLiteral("items")).toArray().at(0).toObject();
        if (item.value(QStringLiteral("id")).toString() != QLatin1String("query")) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP query helper returned the wrong item id."));
            process->deleteLater();
            return;
        }
        QVector<double> queryVector;
        QString vectorError;
        if (!parseUnitVector(item.value(QStringLiteral("vector")), queryVector, &vectorError)) {
            jobs->markFailed(jobId, vectorError);
            process->deleteLater();
            return;
        }
        QString searchError;
        const QList<VibeCutEmbeddingSearchHit> hits = VibeCutEmbeddingStore::cosineSearch(
            currentRoot, queryVector, kModel, kModelRevision, QStringList{QStringLiteral("visual")},
            limit, minSimilarity, &searchError);
        if (!searchError.isEmpty()) {
            jobs->markFailed(jobId, searchError);
            process->deleteLater();
            return;
        }
        QJsonArray jsonHits;
        for (const VibeCutEmbeddingSearchHit &hit : hits) jsonHits.append(hit.toJson());
        const QJsonObject result{{QStringLiteral("kind"), QStringLiteral("semantic_crossmodal_search")},
                                 {QStringLiteral("query"), query},
                                 {QStringLiteral("model"), kModel},
                                 {QStringLiteral("model_revision"), kModelRevision},
                                 {QStringLiteral("dimension"), kDimension},
                                 {QStringLiteral("score_semantics"), QStringLiteral("cosine_similarity_same_embedding_space")},
                                 {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                                 {QStringLiteral("stale_or_unavailable_embeddings_skipped"), staleOrUnavailable},
                                 {QStringLiteral("hit_count"), jsonHits.size()},
                                 {QStringLiteral("hits"), jsonHits}};
        QString resultError;
        if (!jobs->setResult(jobId, result, &resultError)) {
            jobs->markFailed(jobId, QStringLiteral("SigLIP search result was rejected: %1").arg(resultError));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Cross-modal search produced %1 current visual hit(s).").arg(jsonHits.size()));
        process->deleteLater();
    });
    process->start(vibeCutVisionPython(), {helperPath()});
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), jobId}, {QStringLiteral("current_visual_embedding_count"), currentRecords.size()},
                       {QStringLiteral("stale_or_unavailable_embeddings_skipped"), staleOrUnavailable},
                       {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)}};
}

QJsonObject resultTool(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
    if (id.isEmpty()) return err(QStringLiteral("job_id must not be empty."));
    VibeCutJob job;
    if (!tools->jobManager()->job(id, job)) return err(QStringLiteral("Unknown VibeCut job: %1").arg(id));
    if (job.kind != QLatin1String("semantic_visual_refresh") && job.kind != QLatin1String("semantic_crossmodal_search")) {
        return err(QStringLiteral("Job %1 is not a SigLIP semantic job.").arg(id));
    }
    if (!job.terminal()) return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("ready"), false},
                                             {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                                             {QStringLiteral("progress"), job.progress}, {QStringLiteral("message"), job.message}};
    if (job.state != VibeCutJobState::Succeeded) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("ready"), true},
                                                                     {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                                                                     {QStringLiteral("error"), job.message}};
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("ready"), true},
                       {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                       {QStringLiteral("message"), job.message}, {QStringLiteral("result"), job.result}};
}
} // namespace

bool registerVibeCutCrossModalTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Cross-modal semantic tools require native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("semantic_crossmodal_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), statusPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Report the pinned SigLIP image/text embedding space and current visual-vector count. It shares VibeCut's isolated vision runtime; use vision_setup when dependencies are missing.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              statusPolicy, status, error)) return false;

    const QJsonObject refreshProperties{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                        {QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                        {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                        {QStringLiteral("sample_interval_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000000}}},
                                        {QStringLiteral("max_samples"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), kMaxSamples}}},
                                        {QStringLiteral("batch_size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 128}}},
                                        {QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                               {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}}}};
    VibeCutToolPolicy refreshPolicy;
    refreshPolicy.name = QStringLiteral("semantic_visual_refresh");
    refreshPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    refreshPolicy.asynchronous = true;
    refreshPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), refreshPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Asynchronously sample authoritative source frames from one file-backed video bin asset and atomically refresh that source's pinned SigLIP visual embeddings. A source replacement/fingerprint change invalidates old vectors; caller cannot inject file/model paths.")},
                                          {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                       {QStringLiteral("properties"), refreshProperties},
                                                                                       {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                                                                       {QStringLiteral("additionalProperties"), false}}}}},
                              refreshPolicy, [tools, surfacePtr](const QJsonObject &input) { return startVisualRefresh(tools, surfacePtr, input); }, error)) return false;

    const QJsonObject searchProperties{{QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 2048}}},
                                       {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}}},
                                       {QStringLiteral("min_similarity"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), -1.0}, {QStringLiteral("maximum"), 1.0}}},
                                       {QStringLiteral("device"), refreshProperties.value(QStringLiteral("device"))}};
    VibeCutToolPolicy searchPolicy;
    searchPolicy.name = QStringLiteral("semantic_search_visual");
    searchPolicy.risk = VibeCutToolRisk::ReadOnly;
    searchPolicy.asynchronous = true;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), searchPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Asynchronously encode a text query in the exact pinned SigLIP image/text space and rank only current source-fingerprint visual anchors by cosine similarity. Returns a job_id; call semantic_crossmodal_result for the ranked result. Similarity is ranking evidence, not probability.")},
                                          {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                       {QStringLiteral("properties"), searchProperties},
                                                                                       {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}},
                                                                                       {QStringLiteral("additionalProperties"), false}}}}},
                              searchPolicy, [tools, surfacePtr](const QJsonObject &input) { return startCrossModalSearch(tools, surfacePtr, input); }, error)) return false;

    const QJsonObject resultInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("job_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy resultPolicy;
    resultPolicy.name = QStringLiteral("semantic_crossmodal_result");
    resultPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), resultPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Read the state/result of one SigLIP visual-refresh or cross-modal-search job.")},
                                            {QStringLiteral("input_schema"), resultInput}},
                                resultPolicy, [tools](const QJsonObject &input) { return resultTool(tools, input); }, error);
}
