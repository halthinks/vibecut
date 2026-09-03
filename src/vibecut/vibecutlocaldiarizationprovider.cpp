/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutlocaldiarizationprovider.h"

#include "vibecutextractorprovider.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"
#include "vibecutsecretstore.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QtMath>

#include <memory>

namespace {
const QString kProviderId = QStringLiteral("local_pyannote");
const QString kExtractorId = QStringLiteral("local_pyannote_diarization");
const QString kExtractorVersion = QStringLiteral("1.0.0");
const QString kModel = QStringLiteral("pyannote/speaker-diarization-community-1");
const QString kTokenSecret = QStringLiteral("extractor.local_pyannote.hf_token");

QString envToken()
{
    const QString hf = QString::fromLocal8Bit(qgetenv("HF_TOKEN")).trimmed();
    if (!hf.isEmpty()) return hf;
    return QString::fromLocal8Bit(qgetenv("HUGGINGFACE_TOKEN")).trimmed();
}

QString pythonOverride()
{
    return QString::fromLocal8Bit(qgetenv("VIBECUT_PYANNOTE_PYTHON")).trimmed();
}

class LocalPyannoteDiarizationProvider : public VibeCutExtractorProvider
{
public:
    QString id() const override { return kProviderId; }
    QString displayName() const override { return QStringLiteral("Local pyannote Community-1"); }
    QStringList capabilities() const override { return {QStringLiteral("diarization")}; }

    bool configured(QString *error) const override
    {
        if (error) error->clear();
        QString dependencyError;
        if (!vibeCutPyannoteDependenciesReady(&dependencyError)) {
            if (error) *error = dependencyError;
            return false;
        }
        QString tokenError;
        if (vibeCutPyannoteToken(&tokenError).isEmpty()) {
            if (error) *error = tokenError.isEmpty()
                                   ? QStringLiteral("Local pyannote requires a Hugging Face token for the Community-1 model.")
                                   : tokenError;
            return false;
        }
        return true;
    }

    QJsonObject start(const QString &capability,
                      const QJsonObject &input,
                      const VibeCutExtractorProviderContext &context,
                      QString *error) override
    {
        if (error) error->clear();
        if (capability.trimmed().toLower() != QLatin1String("diarization")) {
            if (error) *error = QStringLiteral("Local pyannote only implements diarization.");
            return QJsonObject();
        }
        if (!context.jobs || !context.persistEvidence) {
            if (error) *error = QStringLiteral("Local pyannote requires the shared JobManager and validated evidence sink.");
            return QJsonObject();
        }
        if (!input.value(QStringLiteral("has_audio")).toBool(false)) {
            if (error) *error = QStringLiteral("Diarization requires a source with audio.");
            return QJsonObject();
        }

        QString dependencyError;
        if (!vibeCutPyannoteDependenciesReady(&dependencyError)) {
            if (error) *error = dependencyError;
            return QJsonObject();
        }
        QString tokenError;
        const QString token = vibeCutPyannoteToken(&tokenError);
        if (token.isEmpty()) {
            if (error) *error = tokenError.isEmpty() ? QStringLiteral("No Hugging Face token is configured for local pyannote.") : tokenError;
            return QJsonObject();
        }

        const QString sourcePath = input.value(QStringLiteral("source_path")).toString();
        const QString sourceId = input.value(QStringLiteral("source_id")).toString();
        const QString sourceFingerprint = input.value(QStringLiteral("source_fingerprint")).toString();
        const double fps = input.value(QStringLiteral("fps")).toDouble(0.0);
        const int startFrame = input.value(QStringLiteral("start_frame")).toInt(-1);
        const int endFrame = input.value(QStringLiteral("end_frame")).toInt(-1);
        if (sourcePath.isEmpty() || sourceId.isEmpty() || sourceFingerprint.isEmpty() || fps <= 0.0 || startFrame < 0 || endFrame <= startFrame) {
            if (error) *error = QStringLiteral("Local pyannote received an incomplete normalized extractor request.");
            return QJsonObject();
        }

        const QJsonObject parameters = input.value(QStringLiteral("parameters")).toObject();
        const bool exclusive = parameters.value(QStringLiteral("exclusive")).toBool(true);
        const QString device = parameters.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
        if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) {
            if (error) *error = QStringLiteral("Diarization parameter device must be auto, cpu, or cuda.");
            return QJsonObject();
        }
        const int minSpeakers = parameters.value(QStringLiteral("min_speakers")).toInt(0);
        const int maxSpeakers = parameters.value(QStringLiteral("max_speakers")).toInt(0);
        if (minSpeakers < 0 || maxSpeakers < 0 || minSpeakers > 20 || maxSpeakers > 20 ||
            (minSpeakers > 0 && maxSpeakers > 0 && minSpeakers > maxSpeakers)) {
            if (error) *error = QStringLiteral("Diarization speaker bounds must be 0..20 and min_speakers may not exceed max_speakers.");
            return QJsonObject();
        }
        if (parameters.contains(QStringLiteral("model")) && parameters.value(QStringLiteral("model")).toString() != kModel) {
            if (error) *error = QStringLiteral("The built-in local provider is pinned to %1.").arg(kModel);
            return QJsonObject();
        }

        QStringList arguments{
            vibeCutPyannoteScript(),
            QStringLiteral("--source"), sourcePath,
            QStringLiteral("--start-seconds"), QString::number(static_cast<double>(startFrame) / fps, 'f', 9),
            QStringLiteral("--end-seconds"), QString::number(static_cast<double>(endFrame) / fps, 'f', 9),
            QStringLiteral("--model"), kModel,
            QStringLiteral("--device"), device,
        };
        if (exclusive) arguments << QStringLiteral("--exclusive");
        if (minSpeakers > 0) arguments << QStringLiteral("--min-speakers") << QString::number(minSpeakers);
        if (maxSpeakers > 0) arguments << QStringLiteral("--max-speakers") << QString::number(maxSpeakers);

        const QString jobId = context.jobs->createJob(QStringLiteral("diarization"),
                                                      QStringLiteral("Speaker diarization · %1").arg(sourceId),
                                                      true);
        context.jobs->markRunning(jobId, QStringLiteral("Running local pyannote Community-1…"));

        auto *process = new QProcess(context.jobs);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("HF_TOKEN"), token);
        environment.insert(QStringLiteral("PYANNOTE_METRICS_ENABLED"), QStringLiteral("0"));
        process->setProcessEnvironment(environment);

        QObject::connect(context.jobs, &VibeCutJobManager::jobChanged, process,
                         [jobs = context.jobs, process, jobId](const QString &changedId) {
                             if (changedId != jobId) return;
                             VibeCutJob job;
                             if (!jobs->job(jobId, job)) return;
                             if (job.state == VibeCutJobState::CancelRequested && process->state() != QProcess::NotRunning) {
                                 process->terminate();
                             }
                         });

        const auto persistEvidence = context.persistEvidence;
        QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process,
                         [process, jobs = context.jobs, jobId, persistEvidence, sourceId, sourceFingerprint, fps, startFrame, endFrame]
                         (int exitCode, QProcess::ExitStatus exitStatus) {
                             VibeCutJob current;
                             if (jobs->job(jobId, current) && current.state == VibeCutJobState::CancelRequested) {
                                 jobs->markCancelled(jobId, QStringLiteral("Speaker diarization cancelled."));
                                 process->deleteLater();
                                 return;
                             }
                             if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                                 const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(4000).trimmed();
                                 jobs->markFailed(jobId, stderrText.isEmpty() ? QStringLiteral("Local pyannote exited with code %1.").arg(exitCode) : stderrText);
                                 process->deleteLater();
                                 return;
                             }
                             const QByteArray stdoutData = process->readAllStandardOutput();
                             if (stdoutData.size() > 16 * 1024 * 1024) {
                                 jobs->markFailed(jobId, QStringLiteral("Local pyannote output exceeded the 16 MiB safety limit."));
                                 process->deleteLater();
                                 return;
                             }
                             QJsonParseError parseError;
                             const QJsonDocument document = QJsonDocument::fromJson(stdoutData, &parseError);
                             if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                                 jobs->markFailed(jobId, QStringLiteral("Local pyannote returned malformed JSON: %1").arg(parseError.errorString()));
                                 process->deleteLater();
                                 return;
                             }
                             const QJsonObject root = document.object();
                             if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 || !root.value(QStringLiteral("segments")).isArray()) {
                                 jobs->markFailed(jobId, QStringLiteral("Local pyannote returned an unsupported result schema."));
                                 process->deleteLater();
                                 return;
                             }

                             QList<VibeCutMediaEvidenceRecord> records;
                             for (const QJsonValue &value : root.value(QStringLiteral("segments")).toArray()) {
                                 const QJsonObject segment = value.toObject();
                                 const double startSeconds = segment.value(QStringLiteral("start_seconds")).toDouble(-1.0);
                                 const double endSeconds = segment.value(QStringLiteral("end_seconds")).toDouble(-1.0);
                                 const QString cluster = segment.value(QStringLiteral("speaker_cluster_id")).toString().trimmed();
                                 if (startSeconds < 0.0 || endSeconds <= startSeconds || cluster.isEmpty()) {
                                     jobs->markFailed(jobId, QStringLiteral("Local pyannote returned an invalid speaker segment."));
                                     process->deleteLater();
                                     return;
                                 }
                                 const int segmentStart = qBound(startFrame, static_cast<int>(qRound64(startSeconds * fps)), endFrame);
                                 const int segmentEnd = qBound(startFrame, static_cast<int>(qRound64(endSeconds * fps)), endFrame);
                                 if (segmentEnd <= segmentStart) continue;
                                 VibeCutMediaEvidenceRecord record;
                                 record.kind = QStringLiteral("speaker_segment");
                                 record.startFrame = segmentStart;
                                 record.endFrame = segmentEnd;
                                 record.confidence = -1.0;
                                 record.metadata = QJsonObject{{QStringLiteral("speaker_cluster_id"), cluster},
                                                               {QStringLiteral("overlap"), segment.value(QStringLiteral("overlap")).toBool(false)},
                                                               {QStringLiteral("exclusive"), segment.value(QStringLiteral("exclusive")).toBool(false)},
                                                               {QStringLiteral("model"), root.value(QStringLiteral("model")).toString()},
                                                               {QStringLiteral("device"), root.value(QStringLiteral("device")).toString()}};
                                 records.append(record);
                             }

                             QString persistError;
                             if (!persistEvidence(sourceId, sourceFingerprint, kExtractorId, kExtractorVersion, records, &persistError)) {
                                 jobs->markFailed(jobId, QStringLiteral("Diarization evidence was rejected: %1").arg(persistError));
                                 process->deleteLater();
                                 return;
                             }
                             jobs->markSucceeded(jobId, QStringLiteral("Speaker diarization persisted %1 anonymous segment(s).").arg(records.size()));
                             process->deleteLater();
                         });

        QObject::connect(process, &QProcess::errorOccurred, process,
                         [process, jobs = context.jobs, jobId](QProcess::ProcessError processError) {
                             if (processError != QProcess::FailedToStart) return;
                             jobs->markFailed(jobId, QStringLiteral("Could not launch the configured pyannote Python environment."));
                             process->deleteLater();
                         });

        process->start(vibeCutPyannotePython(), arguments);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("started"), true},
                           {QStringLiteral("job_id"), jobId},
                           {QStringLiteral("model"), kModel},
                           {QStringLiteral("exclusive"), exclusive},
                           {QStringLiteral("device"), device}};
    }
};
}

QString vibeCutPyannoteVenvDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/vibecut-pyannote-venv");
}

QString vibeCutPyannotePython()
{
    const QString overridePath = pythonOverride();
    return overridePath.isEmpty() ? vibeCutPyannoteVenvDir() + QStringLiteral("/bin/python3") : overridePath;
}

QString vibeCutPyannoteScript()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/vibecut/diarize_pyannote.py"));
}

QString vibeCutPyannoteToken(QString *error)
{
    if (error) error->clear();
    const QString environment = envToken();
    if (!environment.isEmpty()) return environment;
    QString secretError;
    const QString secret = VibeCutSecretStore::readSecret(kTokenSecret, &secretError).trimmed();
    if (!secretError.isEmpty() && error) *error = secretError;
    return secret;
}

bool vibeCutStorePyannoteToken(const QString &token, QString *error)
{
    const QString clean = token.trimmed();
    if (clean.isEmpty()) {
        if (error) *error = QStringLiteral("Hugging Face token must not be empty.");
        return false;
    }
    return VibeCutSecretStore::writeSecret(kTokenSecret, clean, error);
}

bool vibeCutPyannoteDependenciesReady(QString *error)
{
    if (error) error->clear();
    const QString script = vibeCutPyannoteScript();
    if (script.isEmpty() || !QFileInfo::exists(script)) {
        if (error) *error = QStringLiteral("VibeCut's installed pyannote diarization helper script was not found.");
        return false;
    }
    const QString python = vibeCutPyannotePython();
    if (python.isEmpty() || !QFileInfo::exists(python)) {
        if (error) *error = QStringLiteral("Pyannote Python environment is missing. Run the VibeCut diarization setup first or set VIBECUT_PYANNOTE_PYTHON.");
        return false;
    }

    QProcess probe;
    probe.start(python, {QStringLiteral("-c"), QStringLiteral("import pyannote.audio, torch; print('ok')")});
    if (!probe.waitForStarted(2000) || !probe.waitForFinished(5000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        if (error) *error = QStringLiteral("Configured Python cannot import pyannote.audio and torch. Run the VibeCut diarization setup.");
        return false;
    }
    return true;
}

void ensureVibeCutBuiltinExtractorProvidersRegistered()
{
    static bool registered = false;
    if (registered) return;
    QString error;
    if (VibeCutExtractorProviderRegistry::global().registerProvider(kProviderId,
                                                                    []() { return std::make_unique<LocalPyannoteDiarizationProvider>(); },
                                                                    &error)) {
        registered = true;
        return;
    }
    // A prior registration with this exact id is acceptable in test/application
    // processes that construct more than one ToolSurface.
    if (VibeCutExtractorProviderRegistry::global().providerIds().contains(kProviderId)) registered = true;
}
