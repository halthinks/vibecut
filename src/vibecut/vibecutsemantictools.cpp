/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsemantictools.h"

#include "vibecutembeddingstore.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaindex.h"
#include "vibecutsemanticruntime.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QDir>
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
const QString kModel = QStringLiteral("sentence-transformers/all-MiniLM-L6-v2");
const QString kModelRevision = QStringLiteral("1110a243fdf4706b3f48f1d95db1a4f5529b4d41");
const QString kProducer = QStringLiteral("semantic_text_minilm");
const QString kProducerVersion = QStringLiteral("1.0.0");
const QString kSentenceTransformersVersion = QStringLiteral("6.0.1");
const QString kTransformersVersion = QStringLiteral("5.16.1");
const QString kTorchVersion = QStringLiteral("2.14.0");
constexpr int kDimension = 384;
constexpr int kMaxDocuments = 5000;

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
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

QString textHash(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool parseVector(const QJsonValue &value, QVector<double> &unit, QString *error)
{
    if (!value.isArray()) {
        if (error) *error = QStringLiteral("Semantic helper returned a non-array embedding.");
        return false;
    }
    const QJsonArray raw = value.toArray();
    if (raw.size() != kDimension) {
        if (error) *error = QStringLiteral("Semantic helper returned dimension %1, expected %2.").arg(raw.size()).arg(kDimension);
        return false;
    }
    QVector<double> vector;
    vector.reserve(raw.size());
    for (const QJsonValue &entry : raw) {
        if (!entry.isDouble() || !std::isfinite(entry.toDouble())) {
            if (error) *error = QStringLiteral("Semantic helper returned a non-finite/non-numeric vector value.");
            return false;
        }
        vector.append(entry.toDouble());
    }
    QString normalizeError;
    if (!VibeCutEmbeddingStore::normalizeVector(vector, unit, &normalizeError)) {
        if (error) *error = normalizeError;
        return false;
    }
    long double dot = 0.0L;
    for (int i = 0; i < vector.size(); ++i) dot += static_cast<long double>(vector.at(i)) * unit.at(i);
    if (std::abs(static_cast<double>(dot) - 1.0) > 0.001) {
        if (error) *error = QStringLiteral("Semantic helper output was not unit-normalized as declared.");
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
        root.value(QStringLiteral("dimension")).toInt(-1) != kDimension ||
        !root.value(QStringLiteral("unit_normalized")).toBool(false) ||
        root.value(QStringLiteral("sentence_transformers_version")).toString() != kSentenceTransformersVersion ||
        root.value(QStringLiteral("transformers_version")).toString() != kTransformersVersion ||
        !root.value(QStringLiteral("torch_version")).toString().startsWith(kTorchVersion) ||
        root.value(QStringLiteral("item_count")).toInt(-1) != expectedCount ||
        !root.value(QStringLiteral("items")).isArray() ||
        root.value(QStringLiteral("items")).toArray().size() != expectedCount) {
        if (error) *error = QStringLiteral("MiniLM helper returned an unsupported or provenance-mismatched result schema.");
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

bool cancelled(VibeCutJobManager *jobs, const QString &jobId, const QString &message)
{
    VibeCutJob job;
    if (!jobs->job(jobId, job) || job.state != VibeCutJobState::CancelRequested) return false;
    jobs->markCancelled(jobId, message);
    return true;
}

void bindFailedStart(QProcess *process, VibeCutJobManager *jobs, const QString &jobId, const QString &label)
{
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, jobs, jobId, label](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && !job.terminal()) jobs->markFailed(jobId, QStringLiteral("Could not launch %1.").arg(label));
        process->deleteLater();
    });
}

void startDependencyInstall(VibeCutJobManager *jobs, const QString &jobId,
                            const QString &python, const QString &requirements)
{
    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process,
                     [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->terminate();
    });
    jobs->setProgress(jobId, 40, QStringLiteral("Installing pinned Sentence Transformers semantic runtime…"));
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId](int exitCode, QProcess::ExitStatus status) {
        if (cancelled(jobs, jobId, QStringLiteral("Semantic runtime setup cancelled."))) {
            process->deleteLater();
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString output = QString::fromUtf8(process->readAll()).right(8000).trimmed();
            jobs->markFailed(jobId, output.isEmpty()
                                        ? QStringLiteral("Installing the pinned semantic runtime failed with code %1.").arg(exitCode)
                                        : output);
            process->deleteLater();
            return;
        }
        QString readyError;
        if (!vibeCutSemanticDependenciesReady(&readyError)) {
            jobs->markFailed(jobId, QStringLiteral("Semantic package installation completed but runtime verification failed: %1").arg(readyError));
            process->deleteLater();
            return;
        }
        jobs->setProgress(jobId, 100, QStringLiteral("Local semantic runtime is ready."));
        jobs->markSucceeded(jobId, QStringLiteral("Installed the pinned MiniLM semantic runtime. The model is acquired on first use if not cached."));
        process->deleteLater();
    });
    bindFailedStart(process, jobs, jobId, QStringLiteral("semantic Python package installer"));
    process->start(python, {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"),
                            QStringLiteral("--disable-pip-version-check"), QStringLiteral("-r"), requirements});
}

QJsonObject status(const QJsonObject &)
{
    QString readyError;
    const bool ready = vibeCutSemanticDependenciesReady(&readyError);
    QString storeError;
    const QJsonObject root = VibeCutEmbeddingStore::loadCurrent(&storeError);
    int matchingRecords = 0;
    if (storeError.isEmpty()) {
        for (const QJsonValue &value : root.value(QStringLiteral("records")).toArray()) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("model")).toString() == kModel &&
                object.value(QStringLiteral("model_revision")).toString() == kModelRevision &&
                object.value(QStringLiteral("producer_id")).toString() == kProducer) ++matchingRecords;
        }
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("model"), kModel},
                       {QStringLiteral("model_revision"), kModelRevision},
                       {QStringLiteral("model_license"), QStringLiteral("Apache-2.0")},
                       {QStringLiteral("dimension"), kDimension},
                       {QStringLiteral("score_semantics"), QStringLiteral("cosine_similarity_same_embedding_space")},
                       {QStringLiteral("python"), vibeCutSemanticPython()},
                       {QStringLiteral("script"), vibeCutSemanticScript()},
                       {QStringLiteral("requirements"), vibeCutSemanticRequirements()},
                       {QStringLiteral("dependencies_ready"), ready},
                       {QStringLiteral("dependency_error"), ready ? QString() : readyError},
                       {QStringLiteral("embedding_store"), VibeCutEmbeddingStore::fileName()},
                       {QStringLiteral("embedding_store_error"), storeError},
                       {QStringLiteral("text_embedding_count"), matchingRecords},
                       {QStringLiteral("ready"), ready},
                       {QStringLiteral("note"), QStringLiteral("MiniLM embeddings are local model representations. Cosine similarity is ranking evidence inside this exact model revision, not semantic fact or probability.")}};
}

QJsonObject setup(VibeCutTools *tools, const QJsonObject &)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("Shared VibeCut JobManager is unavailable."));
    const QString requirements = vibeCutSemanticRequirements();
    if (requirements.isEmpty() || !QFileInfo::exists(requirements)) return err(QStringLiteral("Pinned semantic requirements file is not installed."));
    if (!qEnvironmentVariableIsEmpty("VIBECUT_SEMANTIC_PYTHON")) {
        return err(QStringLiteral("VIBECUT_SEMANTIC_PYTHON points to a user-managed environment. VibeCut will not modify it; install the pinned semantic requirements there or unset the override."));
    }
    QString readyError;
    if (vibeCutSemanticDependenciesReady(&readyError)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), false}, {QStringLiteral("already_ready"), true}};
    }
    VibeCutJobManager *jobs = tools->jobManager();
    const QString jobId = jobs->createJob(QStringLiteral("semantic_setup"), QStringLiteral("Install MiniLM semantic runtime"), true);
    jobs->markRunning(jobId, QStringLiteral("Preparing isolated semantic Python environment…"));
    jobs->setProgress(jobId, 5);
    const QString venvPython = vibeCutSemanticPython();
    if (QFileInfo::exists(venvPython)) {
        startDependencyInstall(jobs, jobId, venvPython, requirements);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true}, {QStringLiteral("job_id"), jobId}};
    }
    const QString systemPython = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (systemPython.isEmpty()) {
        jobs->markFailed(jobId, QStringLiteral("python3 is not available on PATH."));
        return err(QStringLiteral("python3 is required to create the semantic environment."));
    }
    QDir().mkpath(QFileInfo(vibeCutSemanticVenvDir()).absolutePath());
    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process,
                     [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->terminate();
    });
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId, venvPython, requirements](int exitCode, QProcess::ExitStatus processStatus) {
        if (cancelled(jobs, jobId, QStringLiteral("Semantic runtime setup cancelled."))) {
            process->deleteLater();
            return;
        }
        if (processStatus != QProcess::NormalExit || exitCode != 0 || !QFileInfo::exists(venvPython)) {
            const QString output = QString::fromUtf8(process->readAll()).right(8000).trimmed();
            jobs->markFailed(jobId, output.isEmpty() ? QStringLiteral("Creating the semantic virtual environment failed.") : output);
            process->deleteLater();
            return;
        }
        process->deleteLater();
        startDependencyInstall(jobs, jobId, venvPython, requirements);
    });
    bindFailedStart(process, jobs, jobId, QStringLiteral("python3 for semantic environment setup"));
    process->start(systemPython, {QStringLiteral("-m"), QStringLiteral("venv"), vibeCutSemanticVenvDir()});
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true}, {QStringLiteral("job_id"), jobId}};
}

QJsonObject startRefresh(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("Semantic refresh requires the VibeCut runtime."));
    QString readyError;
    if (!vibeCutSemanticDependenciesReady(&readyError)) return err(readyError);
    QString storeError;
    VibeCutEmbeddingStore::loadCurrent(&storeError);
    if (!storeError.isEmpty()) return err(storeError);

    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) return err(indexError);
    QList<VibeCutMediaDocument> documents;
    QHash<QString, VibeCutMediaDocument> byId;
    QJsonArray requestItems;
    int totalChars = 0;
    for (const VibeCutMediaDocument &document : index.documents()) {
        if (document.kind != QLatin1String("transcript") && document.kind != QLatin1String("ocr_text")) continue;
        const QString text = document.text.trimmed();
        if (text.isEmpty()) continue;
        if (text.size() > 8192) return err(QStringLiteral("Semantic document %1 exceeds the 8192-character per-item limit.").arg(document.id));
        totalChars += text.size();
        if (totalChars > 8000000) return err(QStringLiteral("Current transcript/OCR corpus exceeds the 8,000,000-character semantic refresh limit."));
        if (documents.size() >= kMaxDocuments) return err(QStringLiteral("Current transcript/OCR corpus exceeds the %1-document semantic refresh limit.").arg(kMaxDocuments));
        if (byId.contains(document.id)) return err(QStringLiteral("Canonical media index contains duplicate document id: %1").arg(document.id));
        documents.append(document);
        byId.insert(document.id, document);
        requestItems.append(QJsonObject{{QStringLiteral("id"), document.id}, {QStringLiteral("text"), text}});
    }
    if (documents.isEmpty()) return err(QStringLiteral("No transcript or OCR text documents are available to embed."));

    const QString device = input.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
    if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) return err(QStringLiteral("device must be auto, cpu, or cuda."));
    const int batchSize = input.value(QStringLiteral("batch_size")).toInt(32);
    if (batchSize < 1 || batchSize > 256) return err(QStringLiteral("batch_size must be 1..256."));

    const QJsonObject request{{QStringLiteral("schema_version"), 1},
                              {QStringLiteral("operation"), QStringLiteral("documents")},
                              {QStringLiteral("model"), kModel},
                              {QStringLiteral("model_revision"), kModelRevision},
                              {QStringLiteral("device"), device},
                              {QStringLiteral("batch_size"), batchSize},
                              {QStringLiteral("items"), requestItems}};
    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (payload.size() > 16 * 1024 * 1024) return err(QStringLiteral("Semantic refresh request exceeds the 16 MiB helper limit."));

    VibeCutJobManager *jobs = tools->jobManager();
    const quint64 baseRevision = surface->projectRevision();
    const QString jobId = jobs->createJob(QStringLiteral("semantic_text_refresh"), QStringLiteral("Embed transcript and OCR text"), true);
    jobs->markRunning(jobId, QStringLiteral("Embedding canonical transcript/OCR documents with MiniLM…"));
    auto *process = new QProcess(jobs);
    configureProcess(process, jobs, jobId);
    writeRequestWhenStarted(process, payload);
    bindFailedStart(process, jobs, jobId, QStringLiteral("MiniLM semantic helper"));

    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId, surface, baseRevision, documents, byId]
                     (int exitCode, QProcess::ExitStatus processStatus) {
        if (cancelled(jobs, jobId, QStringLiteral("Semantic text refresh cancelled."))) {
            process->deleteLater();
            return;
        }
        if (surface->projectRevision() != baseRevision) {
            jobs->markFailed(jobId, QStringLiteral("Project revision changed while text embeddings were running; refusing stale embedding promotion."));
            process->deleteLater();
            return;
        }
        if (processStatus != QProcess::NormalExit || exitCode != 0) {
            const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(8000).trimmed();
            jobs->markFailed(jobId, stderrText.isEmpty() ? QStringLiteral("MiniLM semantic refresh failed with code %1.").arg(exitCode) : stderrText);
            process->deleteLater();
            return;
        }
        const QByteArray stdoutData = process->readAllStandardOutput();
        if (stdoutData.size() > 64 * 1024 * 1024) {
            jobs->markFailed(jobId, QStringLiteral("MiniLM semantic output exceeded the 64 MiB safety limit."));
            process->deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument responseDocument = QJsonDocument::fromJson(stdoutData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !responseDocument.isObject()) {
            jobs->markFailed(jobId, QStringLiteral("MiniLM semantic helper returned malformed JSON: %1").arg(parseError.errorString()));
            process->deleteLater();
            return;
        }
        const QJsonObject root = responseDocument.object();
        QString schemaError;
        if (!validateHelperRoot(root, QStringLiteral("documents"), documents.size(), &schemaError)) {
            jobs->markFailed(jobId, schemaError);
            process->deleteLater();
            return;
        }
        QSet<QString> seen;
        QList<VibeCutEmbeddingRecord> records;
        for (const QJsonValue &value : root.value(QStringLiteral("items")).toArray()) {
            if (!value.isObject()) {
                jobs->markFailed(jobId, QStringLiteral("MiniLM semantic helper returned a non-object embedding item."));
                process->deleteLater();
                return;
            }
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!byId.contains(id) || seen.contains(id)) {
                jobs->markFailed(jobId, QStringLiteral("MiniLM semantic helper returned an unknown or duplicate document id."));
                process->deleteLater();
                return;
            }
            seen.insert(id);
            QVector<double> vector;
            QString vectorError;
            if (!parseVector(item.value(QStringLiteral("vector")), vector, &vectorError)) {
                jobs->markFailed(jobId, vectorError);
                process->deleteLater();
                return;
            }
            const VibeCutMediaDocument document = byId.value(id);
            VibeCutEmbeddingRecord record;
            record.anchorKind = document.kind;
            record.anchorId = document.id;
            record.sourceId = document.metadata.value(QStringLiteral("source_id")).toString();
            record.sourceFingerprint = document.metadata.value(QStringLiteral("source_fingerprint")).toString();
            record.modality = QStringLiteral("text");
            record.model = kModel;
            record.modelRevision = kModelRevision;
            record.producerId = kProducer;
            record.producerVersion = kProducerVersion;
            record.startFrame = document.startFrame;
            record.endFrame = document.endFrame;
            record.vector = vector;
            record.metadata = QJsonObject{{QStringLiteral("authority"), QStringLiteral("model_representation")},
                                          {QStringLiteral("score_semantics"), QStringLiteral("cosine_similarity_same_embedding_space")},
                                          {QStringLiteral("document_kind"), document.kind},
                                          {QStringLiteral("text_sha256"), textHash(document.text.trimmed())},
                                          {QStringLiteral("evidence_origin"), document.metadata.value(QStringLiteral("evidence_origin"))}};
            records.append(record);
        }
        if (seen.size() != documents.size()) {
            jobs->markFailed(jobId, QStringLiteral("MiniLM semantic helper did not return every requested document embedding."));
            process->deleteLater();
            return;
        }
        QString persistError;
        if (!VibeCutEmbeddingStore::replaceProducerModelCurrent(kProducer, kProducerVersion, kModel, kModelRevision, records, &persistError)) {
            jobs->markFailed(jobId, QStringLiteral("Semantic embedding promotion failed: %1").arg(persistError));
            process->deleteLater();
            return;
        }
        QJsonObject result{{QStringLiteral("kind"), QStringLiteral("semantic_text_refresh")},
                           {QStringLiteral("record_count"), records.size()},
                           {QStringLiteral("model"), kModel},
                           {QStringLiteral("model_revision"), kModelRevision},
                           {QStringLiteral("dimension"), kDimension},
                           {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)}};
        QString resultError;
        if (!jobs->setResult(jobId, result, &resultError)) {
            jobs->markFailed(jobId, QStringLiteral("Semantic embeddings persisted but job result could not be recorded: %1").arg(resultError));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Persisted %1 current transcript/OCR MiniLM embedding(s).").arg(records.size()));
        process->deleteLater();
    });
    process->start(vibeCutSemanticPython(), {vibeCutSemanticScript()});
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), jobId}, {QStringLiteral("document_count"), documents.size()},
                       {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                       {QStringLiteral("model"), kModel}, {QStringLiteral("model_revision"), kModelRevision}};
}

QJsonObject startSearch(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("Semantic search requires the VibeCut runtime."));
    QString readyError;
    if (!vibeCutSemanticDependenciesReady(&readyError)) return err(readyError);
    const QString query = input.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty() || query.size() > 2048) return err(QStringLiteral("query must contain 1..2048 characters."));
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(25), 100);
    const double minSimilarity = qBound(-1.0, input.value(QStringLiteral("min_similarity")).toDouble(-1.0), 1.0);
    const QString device = input.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
    if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) return err(QStringLiteral("device must be auto, cpu, or cuda."));

    QString storeError;
    const QJsonObject embeddingRoot = VibeCutEmbeddingStore::loadCurrent(&storeError);
    if (!storeError.isEmpty()) return err(storeError);
    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) return err(indexError);
    int initialStaleSkipped = 0;
    QString freshnessError;
    const QJsonObject initialCurrentRoot = filterVibeCutCurrentSemanticTextEmbeddingRoot(
        embeddingRoot, index.documents(), &initialStaleSkipped, &freshnessError);
    if (!freshnessError.isEmpty()) return err(freshnessError);
    const int compatibleCount = initialCurrentRoot.value(QStringLiteral("records")).toArray().size();
    if (compatibleCount == 0) return err(QStringLiteral("No current MiniLM text embeddings are available after freshness validation. Run semantic_text_refresh first."));

    const quint64 baseRevision = surface->projectRevision();
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
    const QString jobId = jobs->createJob(QStringLiteral("semantic_text_search"), QStringLiteral("Semantic search · %1").arg(query.left(80)), true);
    jobs->markRunning(jobId, QStringLiteral("Encoding semantic query with MiniLM…"));
    auto *process = new QProcess(jobs);
    configureProcess(process, jobs, jobId);
    writeRequestWhenStarted(process, payload);
    bindFailedStart(process, jobs, jobId, QStringLiteral("MiniLM semantic query helper"));
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId, surface, baseRevision, query, limit, minSimilarity, initialStaleSkipped]
                     (int exitCode, QProcess::ExitStatus processStatus) {
        if (cancelled(jobs, jobId, QStringLiteral("Semantic search cancelled."))) {
            process->deleteLater();
            return;
        }
        if (surface->projectRevision() != baseRevision) {
            jobs->markFailed(jobId, QStringLiteral("Project revision changed while semantic search was running; refusing a stale ranked result."));
            process->deleteLater();
            return;
        }
        if (processStatus != QProcess::NormalExit || exitCode != 0) {
            const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(8000).trimmed();
            jobs->markFailed(jobId, stderrText.isEmpty() ? QStringLiteral("MiniLM semantic query failed with code %1.").arg(exitCode) : stderrText);
            process->deleteLater();
            return;
        }
        const QByteArray stdoutData = process->readAllStandardOutput();
        if (stdoutData.size() > 2 * 1024 * 1024) {
            jobs->markFailed(jobId, QStringLiteral("MiniLM semantic query output exceeded the 2 MiB safety limit."));
            process->deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument responseDocument = QJsonDocument::fromJson(stdoutData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !responseDocument.isObject()) {
            jobs->markFailed(jobId, QStringLiteral("MiniLM semantic query returned malformed JSON: %1").arg(parseError.errorString()));
            process->deleteLater();
            return;
        }
        const QJsonObject root = responseDocument.object();
        QString schemaError;
        if (!validateHelperRoot(root, QStringLiteral("query"), 1, &schemaError)) {
            jobs->markFailed(jobId, schemaError);
            process->deleteLater();
            return;
        }
        const QJsonArray items = root.value(QStringLiteral("items")).toArray();
        if (items.at(0).toObject().value(QStringLiteral("id")).toString() != QLatin1String("query")) {
            jobs->markFailed(jobId, QStringLiteral("MiniLM semantic query returned the wrong item id."));
            process->deleteLater();
            return;
        }
        QVector<double> queryVector;
        QString vectorError;
        if (!parseVector(items.at(0).toObject().value(QStringLiteral("vector")), queryVector, &vectorError)) {
            jobs->markFailed(jobId, vectorError);
            process->deleteLater();
            return;
        }

        QString liveStoreError;
        const QJsonObject liveEmbeddingRoot = VibeCutEmbeddingStore::loadCurrent(&liveStoreError);
        if (!liveStoreError.isEmpty()) {
            jobs->markFailed(jobId, QStringLiteral("Semantic embedding store changed or became invalid while search was running: %1").arg(liveStoreError));
            process->deleteLater();
            return;
        }
        VibeCutMediaIndex liveIndex;
        QString liveIndexError;
        if (!liveIndex.rebuildFromCurrentProject(&liveIndexError)) {
            jobs->markFailed(jobId, QStringLiteral("Canonical media index could not be rebuilt before semantic ranking: %1").arg(liveIndexError));
            process->deleteLater();
            return;
        }
        int staleSkipped = 0;
        QString freshnessError;
        const QJsonObject currentEmbeddingRoot = filterVibeCutCurrentSemanticTextEmbeddingRoot(
            liveEmbeddingRoot, liveIndex.documents(), &staleSkipped, &freshnessError);
        if (!freshnessError.isEmpty()) {
            jobs->markFailed(jobId, freshnessError);
            process->deleteLater();
            return;
        }
        const int currentEmbeddingCount = currentEmbeddingRoot.value(QStringLiteral("records")).toArray().size();
        if (currentEmbeddingCount == 0) {
            jobs->markFailed(jobId, QStringLiteral("No current MiniLM embeddings remain after completion-time freshness validation; refresh embeddings before searching."));
            process->deleteLater();
            return;
        }
        QHash<QString, VibeCutMediaDocument> documents;
        for (const VibeCutMediaDocument &document : liveIndex.documents()) documents.insert(document.id, document);

        QString searchError;
        const QList<VibeCutEmbeddingSearchHit> hits = VibeCutEmbeddingStore::cosineSearch(
            currentEmbeddingRoot, queryVector, kModel, kModelRevision, QStringList{QStringLiteral("text")},
            limit, minSimilarity, &searchError);
        if (!searchError.isEmpty()) {
            jobs->markFailed(jobId, searchError);
            process->deleteLater();
            return;
        }
        QJsonArray outputHits;
        for (const VibeCutEmbeddingSearchHit &hit : hits) {
            const VibeCutMediaDocument document = documents.value(hit.anchorId);
            if (document.id.isEmpty()) {
                jobs->markFailed(jobId, QStringLiteral("Fresh semantic ranking returned an anchor absent from the canonical media index."));
                process->deleteLater();
                return;
            }
            QJsonObject object = hit.toJson();
            object.insert(QStringLiteral("anchor_current"), true);
            object.insert(QStringLiteral("freshness_verified_before_ranking"), true);
            object.insert(QStringLiteral("text"), document.text.left(2048));
            object.insert(QStringLiteral("document_kind"), document.kind);
            outputHits.append(object);
        }
        const QJsonObject result{{QStringLiteral("kind"), QStringLiteral("semantic_text_search")},
                                 {QStringLiteral("query"), query},
                                 {QStringLiteral("model"), kModel},
                                 {QStringLiteral("model_revision"), kModelRevision},
                                 {QStringLiteral("dimension"), kDimension},
                                 {QStringLiteral("score_semantics"), QStringLiteral("cosine_similarity_same_embedding_space")},
                                 {QStringLiteral("freshness_semantics"), QStringLiteral("exact_producer_model_anchor_range_source_fingerprint_and_full_text_hash_filtered_before_cosine_ranking")},
                                 {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                                 {QStringLiteral("current_compatible_embedding_count"), currentEmbeddingCount},
                                 {QStringLiteral("stale_embedding_records_excluded_before_ranking"), staleSkipped},
                                 {QStringLiteral("initial_stale_embedding_records_detected"), initialStaleSkipped},
                                 {QStringLiteral("hit_count"), outputHits.size()},
                                 {QStringLiteral("hits"), outputHits}};
        QString resultError;
        if (!jobs->setResult(jobId, result, &resultError)) {
            jobs->markFailed(jobId, QStringLiteral("Semantic search completed but its structured result was rejected: %1").arg(resultError));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Semantic search produced %1 current-only ranked hit(s); %2 stale MiniLM record(s) were excluded before ranking.")
                                        .arg(outputHits.size()).arg(staleSkipped));
        process->deleteLater();
    });
    process->start(vibeCutSemanticPython(), {vibeCutSemanticScript()});
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), jobId}, {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                       {QStringLiteral("compatible_embedding_count"), compatibleCount},
                       {QStringLiteral("initial_stale_embedding_records_detected"), initialStaleSkipped}};
}

QJsonObject resultTool(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
    if (id.isEmpty()) return err(QStringLiteral("job_id must not be empty."));
    VibeCutJob job;
    if (!tools->jobManager()->job(id, job)) return err(QStringLiteral("Unknown VibeCut job: %1").arg(id));
    if (job.kind != QLatin1String("semantic_text_search") && job.kind != QLatin1String("semantic_text_refresh") &&
        job.kind != QLatin1String("semantic_setup")) {
        return err(QStringLiteral("Job %1 is not a semantic job.").arg(id));
    }
    if (!job.terminal()) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("ready"), false},
                           {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                           {QStringLiteral("progress"), job.progress}, {QStringLiteral("message"), job.message}};
    }
    if (job.state != VibeCutJobState::Succeeded) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("ready"), true},
                           {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                           {QStringLiteral("error"), job.message}};
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("ready"), true},
                       {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                       {QStringLiteral("message"), job.message}, {QStringLiteral("result"), job.result}};
}
} // namespace

bool registerVibeCutSemanticTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Semantic tools require native VibeCutTools/JobManager.");
        return false;
    }
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};

    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("semantic_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), statusPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Report the isolated MiniLM text-semantic runtime and current project embedding-store status, including exact model revision/dimension. Similarity is model-space ranking evidence, not factual probability.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              statusPolicy, status, error)) return false;

    VibeCutToolPolicy setupPolicy;
    setupPolicy.name = QStringLiteral("semantic_setup");
    setupPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    setupPolicy.asynchronous = true;
    setupPolicy.confirmationRequired = true;
    setupPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), setupPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Create a VibeCut-owned isolated Python environment and install the pinned MiniLM/Sentence Transformers semantic runtime. This downloads packages, is cancellable and always requires confirmation.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              setupPolicy, [tools](const QJsonObject &input) { return setup(tools, input); }, error)) return false;

    const QJsonObject runtimeProperties{{QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                               {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}}},
                                        {QStringLiteral("batch_size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                                   {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 256}}}};
    VibeCutToolPolicy refreshPolicy;
    refreshPolicy.name = QStringLiteral("semantic_text_refresh");
    refreshPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    refreshPolicy.asynchronous = true;
    refreshPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), refreshPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Asynchronously rebuild the current project MiniLM text embedding slice from the canonical media index's transcript and OCR documents. The refresh is revision-bound and atomically removes stale anchors/fingerprints for this producer/model before installing the new normalized vectors.")},
                                          {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                       {QStringLiteral("properties"), runtimeProperties},
                                                                                       {QStringLiteral("additionalProperties"), false}}}}},
                              refreshPolicy, [tools, &surface](const QJsonObject &input) { return startRefresh(tools, &surface, input); }, error)) return false;

    QJsonObject searchProperties{{QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 2048}}},
                                 {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}}},
                                 {QStringLiteral("min_similarity"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), -1.0}, {QStringLiteral("maximum"), 1.0}}},
                                 {QStringLiteral("device"), runtimeProperties.value(QStringLiteral("device"))}};
    VibeCutToolPolicy searchPolicy;
    searchPolicy.name = QStringLiteral("semantic_search_text");
    searchPolicy.risk = VibeCutToolRisk::ReadOnly;
    searchPolicy.asynchronous = true;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), searchPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Asynchronously encode a text query with the exact pinned MiniLM embedding space and rank only current transcript/OCR anchors by cosine similarity. Stored records are filtered before ranking by exact producer/model, anchor kind/range, source ID/fingerprint and full-text SHA, and freshness is revalidated when the async query completes. Similarity is not semantic truth or probability.")},
                                          {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                       {QStringLiteral("properties"), searchProperties},
                                                                                       {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}},
                                                                                       {QStringLiteral("additionalProperties"), false}}}}},
                              searchPolicy, [tools, &surface](const QJsonObject &input) { return startSearch(tools, &surface, input); }, error)) return false;

    const QJsonObject resultInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("job_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy resultPolicy;
    resultPolicy.name = QStringLiteral("semantic_result");
    resultPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), resultPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Read the state/result of one semantic setup, refresh or search job. Successful semantic search jobs return their bounded current-only ranked hit payload here.")},
                                            {QStringLiteral("input_schema"), resultInput}},
                                resultPolicy, [tools](const QJsonObject &input) { return resultTool(tools, input); }, error);
}
