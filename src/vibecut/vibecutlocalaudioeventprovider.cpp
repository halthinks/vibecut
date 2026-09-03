/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutlocalaudioeventprovider.h"

#include "kdenlivesettings.h"
#include "vibecutextractorprovider.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>
#include <QtMath>

#include <cmath>
#include <memory>

namespace {
const QString kProviderId = QStringLiteral("local_ast_audioset");
const QString kExtractorId = QStringLiteral("local_ast_audioset");
const QString kExtractorVersion = QStringLiteral("1.0.0");
const QString kModel = QStringLiteral("MIT/ast-finetuned-audioset-10-10-0.4593");
const QString kTransformersVersion = QStringLiteral("5.16.1");
const QString kTorchVersion = QStringLiteral("2.14.0");
constexpr double MaxDecodeSeconds = 1800.0;

QString pythonOverride()
{
    return QString::fromLocal8Bit(qgetenv("VIBECUT_AUDIO_EVENTS_PYTHON")).trimmed();
}

bool readBoundedDouble(const QJsonObject &parameters,
                       const QString &name,
                       double defaultValue,
                       double minimum,
                       double maximum,
                       double &result,
                       QString *error)
{
    if (!parameters.contains(name)) {
        result = defaultValue;
        return true;
    }
    const QJsonValue value = parameters.value(name);
    if (!value.isDouble()) {
        if (error) *error = QStringLiteral("Audio-event parameter %1 must be numeric.").arg(name);
        return false;
    }
    result = value.toDouble();
    if (!std::isfinite(result) || result < minimum || result > maximum) {
        if (error) *error = QStringLiteral("Audio-event parameter %1 must be between %2 and %3.")
                               .arg(name).arg(minimum).arg(maximum);
        return false;
    }
    return true;
}

bool readBoundedInt(const QJsonObject &parameters,
                    const QString &name,
                    int defaultValue,
                    int minimum,
                    int maximum,
                    int &result,
                    QString *error)
{
    if (!parameters.contains(name)) {
        result = defaultValue;
        return true;
    }
    const QJsonValue value = parameters.value(name);
    if (!value.isDouble()) {
        if (error) *error = QStringLiteral("Audio-event parameter %1 must be an integer.").arg(name);
        return false;
    }
    const double raw = value.toDouble();
    const int converted = value.toInt(minimum - 1);
    if (!std::isfinite(raw) || static_cast<double>(converted) != raw || converted < minimum || converted > maximum) {
        if (error) *error = QStringLiteral("Audio-event parameter %1 must be an integer from %2 to %3.")
                               .arg(name).arg(minimum).arg(maximum);
        return false;
    }
    result = converted;
    return true;
}

class LocalAstAudioEventProvider : public VibeCutExtractorProvider
{
public:
    QString id() const override { return kProviderId; }
    QString displayName() const override { return QStringLiteral("Local MIT AST AudioSet"); }
    QStringList capabilities() const override { return {QStringLiteral("audio_events")}; }

    bool configured(QString *error) const override
    {
        return vibeCutAudioEventDependenciesReady(error);
    }

    QJsonObject start(const QString &capability,
                      const QJsonObject &input,
                      const VibeCutExtractorProviderContext &context,
                      QString *error) override
    {
        if (error) error->clear();
        if (capability.trimmed().toLower() != QLatin1String("audio_events")) {
            if (error) *error = QStringLiteral("Local AST only implements audio_events.");
            return QJsonObject();
        }
        if (!context.jobs || !context.persistEvidence) {
            if (error) *error = QStringLiteral("Local AST requires the shared JobManager and validated evidence sink.");
            return QJsonObject();
        }
        if (!input.value(QStringLiteral("has_audio")).toBool(false)) {
            if (error) *error = QStringLiteral("Audio-event classification requires a source with audio.");
            return QJsonObject();
        }

        QString dependencyError;
        if (!vibeCutAudioEventDependenciesReady(&dependencyError)) {
            if (error) *error = dependencyError;
            return QJsonObject();
        }

        const QString sourcePath = input.value(QStringLiteral("source_path")).toString();
        const QString sourceId = input.value(QStringLiteral("source_id")).toString();
        const QString sourceFingerprint = input.value(QStringLiteral("source_fingerprint")).toString();
        const double fps = input.value(QStringLiteral("fps")).toDouble(0.0);
        const int startFrame = input.value(QStringLiteral("start_frame")).toInt(-1);
        const int endFrame = input.value(QStringLiteral("end_frame")).toInt(-1);
        if (sourcePath.isEmpty() || sourceId.isEmpty() || sourceFingerprint.isEmpty() || fps <= 0.0 || startFrame < 0 || endFrame <= startFrame) {
            if (error) *error = QStringLiteral("Local AST received an incomplete normalized extractor request.");
            return QJsonObject();
        }
        const QString ffmpeg = KdenliveSettings::ffmpegpath();
        if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) {
            if (error) *error = QStringLiteral("Kdenlive has no valid configured FFmpeg executable for audio-event decoding.");
            return QJsonObject();
        }

        const QJsonObject parameters = input.value(QStringLiteral("parameters")).toObject();
        if (parameters.contains(QStringLiteral("model")) && parameters.value(QStringLiteral("model")).toString() != kModel) {
            if (error) *error = QStringLiteral("The built-in local audio-event provider is pinned to %1.").arg(kModel);
            return QJsonObject();
        }
        double windowSeconds = 10.0;
        double hopSeconds = 5.0;
        double minScore = 0.05;
        int maxWindows = 120;
        int topK = 8;
        if (!readBoundedDouble(parameters, QStringLiteral("window_seconds"), 10.0, 1.0, 10.0, windowSeconds, error) ||
            !readBoundedDouble(parameters, QStringLiteral("hop_seconds"), 5.0, 0.25, 10.0, hopSeconds, error) ||
            !readBoundedDouble(parameters, QStringLiteral("min_score"), 0.05, 0.0, 1.0, minScore, error) ||
            !readBoundedInt(parameters, QStringLiteral("max_windows"), 120, 1, 500, maxWindows, error) ||
            !readBoundedInt(parameters, QStringLiteral("top_k"), 8, 1, 20, topK, error)) {
            return QJsonObject();
        }
        if (hopSeconds > windowSeconds) {
            if (error) *error = QStringLiteral("Audio-event hop_seconds may not exceed window_seconds because the built-in track analysis must not introduce unobserved gaps between classifier windows.");
            return QJsonObject();
        }
        const QString device = parameters.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
        if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) {
            if (error) *error = QStringLiteral("Audio-event parameter device must be auto, cpu, or cuda.");
            return QJsonObject();
        }

        const double durationSeconds = static_cast<double>(endFrame - startFrame) / fps;
        if (durationSeconds <= 0.0 || durationSeconds > MaxDecodeSeconds) {
            if (error) *error = QStringLiteral("Audio-event excerpt must be positive and at most %1 seconds; split longer sources into bounded ranges.").arg(MaxDecodeSeconds, 0, 'f', 0);
            return QJsonObject();
        }
        const int requiredWindows = durationSeconds <= windowSeconds
                                        ? 1
                                        : 1 + static_cast<int>(std::ceil((durationSeconds - windowSeconds) / hopSeconds));
        if (requiredWindows > maxWindows) {
            if (error) *error = QStringLiteral("Audio-event range/cadence requires %1 windows, exceeding max_windows=%2; increase hop_seconds or use a smaller source range.")
                                   .arg(requiredWindows).arg(maxWindows);
            return QJsonObject();
        }

        const QStringList arguments{
            vibeCutAudioEventScript(),
            QStringLiteral("--source"), sourcePath,
            QStringLiteral("--ffmpeg"), ffmpeg,
            QStringLiteral("--fps"), QString::number(fps, 'f', 9),
            QStringLiteral("--start-frame"), QString::number(startFrame),
            QStringLiteral("--end-frame"), QString::number(endFrame),
            QStringLiteral("--model"), kModel,
            QStringLiteral("--window-seconds"), QString::number(windowSeconds, 'f', 3),
            QStringLiteral("--hop-seconds"), QString::number(hopSeconds, 'f', 3),
            QStringLiteral("--max-windows"), QString::number(maxWindows),
            QStringLiteral("--top-k"), QString::number(topK),
            QStringLiteral("--min-score"), QString::number(minScore, 'f', 6),
            QStringLiteral("--device"), device,
        };

        const QString jobId = context.jobs->createJob(QStringLiteral("audio_events"),
                                                      QStringLiteral("AudioSet event classification · %1").arg(sourceId),
                                                      true);
        context.jobs->markRunning(jobId, QStringLiteral("Running local MIT AST AudioSet classification…"));

        auto *process = new QProcess(context.jobs);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"), QStringLiteral("1"));
        environment.insert(QStringLiteral("DO_NOT_TRACK"), QStringLiteral("1"));
        process->setProcessEnvironment(environment);

        QObject::connect(context.jobs, &VibeCutJobManager::jobChanged, process,
                         [jobs = context.jobs, process, jobId](const QString &changedId) {
                             if (changedId != jobId || process->state() == QProcess::NotRunning) return;
                             VibeCutJob job;
                             if (!jobs->job(jobId, job)) return;
                             if (job.state == VibeCutJobState::CancelRequested) process->terminate();
                         });

        const auto persistEvidence = context.persistEvidence;
        QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                         [process, jobs = context.jobs, jobId, persistEvidence, sourceId, sourceFingerprint, fps,
                          startFrame, endFrame, durationSeconds, requiredWindows, topK, minScore]
                         (int exitCode, QProcess::ExitStatus exitStatus) {
            VibeCutJob current;
            if (jobs->job(jobId, current) && current.state == VibeCutJobState::CancelRequested) {
                jobs->markCancelled(jobId, QStringLiteral("Audio-event classification cancelled."));
                process->deleteLater();
                return;
            }
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(6000).trimmed();
                jobs->markFailed(jobId, stderrText.isEmpty()
                                            ? QStringLiteral("Local AST audio-event classifier exited with code %1.").arg(exitCode)
                                            : stderrText);
                process->deleteLater();
                return;
            }
            const QByteArray stdoutData = process->readAllStandardOutput();
            if (stdoutData.size() > 32 * 1024 * 1024) {
                jobs->markFailed(jobId, QStringLiteral("Local AST output exceeded the 32 MiB safety limit."));
                process->deleteLater();
                return;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(stdoutData, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                jobs->markFailed(jobId, QStringLiteral("Local AST returned malformed JSON: %1").arg(parseError.errorString()));
                process->deleteLater();
                return;
            }
            const QJsonObject root = document.object();
            const QString rootTransformers = root.value(QStringLiteral("transformers_version")).toString().trimmed();
            const QString rootTorch = root.value(QStringLiteral("torch_version")).toString().trimmed();
            if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
                root.value(QStringLiteral("authority")).toString() != QLatin1String("model_prediction") ||
                root.value(QStringLiteral("taxonomy")).toString() != QLatin1String("AudioSet") ||
                root.value(QStringLiteral("model")).toString() != kModel ||
                rootTransformers != kTransformersVersion || !rootTorch.startsWith(kTorchVersion) ||
                !root.value(QStringLiteral("windows")).isArray()) {
                jobs->markFailed(jobId, QStringLiteral("Local AST returned an unsupported or provenance-mismatched result schema."));
                process->deleteLater();
                return;
            }

            QList<VibeCutMediaEvidenceRecord> records;
            int recordIndex = 0;
            const QJsonArray windows = root.value(QStringLiteral("windows")).toArray();
            if (windows.isEmpty() || windows.size() > requiredWindows || root.value(QStringLiteral("window_count")).toInt(-1) != windows.size()) {
                jobs->markFailed(jobId, QStringLiteral("Local AST returned an invalid bounded window count."));
                process->deleteLater();
                return;
            }
            const double timestampTolerance = 1.0 / fps + 0.001;
            for (const QJsonValue &windowValue : windows) {
                if (!windowValue.isObject()) {
                    jobs->markFailed(jobId, QStringLiteral("Local AST returned a non-object window."));
                    process->deleteLater();
                    return;
                }
                const QJsonObject window = windowValue.toObject();
                const QJsonValue windowIndexValue = window.value(QStringLiteral("index"));
                const double rawWindowIndex = windowIndexValue.toDouble(-1.0);
                const int windowIndex = windowIndexValue.toInt(-1);
                const double relativeStartSeconds = window.value(QStringLiteral("start_seconds")).toDouble(-1.0);
                const double relativeEndSeconds = window.value(QStringLiteral("end_seconds")).toDouble(-1.0);
                if (!windowIndexValue.isDouble() || windowIndex < 0 || static_cast<double>(windowIndex) != rawWindowIndex ||
                    relativeStartSeconds < 0.0 || relativeEndSeconds <= relativeStartSeconds ||
                    relativeStartSeconds >= durationSeconds + timestampTolerance ||
                    relativeEndSeconds > durationSeconds + timestampTolerance) {
                    jobs->markFailed(jobId, QStringLiteral("Local AST returned invalid or out-of-range window metadata."));
                    process->deleteLater();
                    return;
                }
                const int calculatedStart = startFrame + static_cast<int>(qRound64(relativeStartSeconds * fps));
                const int calculatedEnd = startFrame + static_cast<int>(qRound64(qMin(relativeEndSeconds, durationSeconds) * fps));
                if (calculatedStart < startFrame || calculatedStart >= endFrame || calculatedEnd <= calculatedStart || calculatedEnd > endFrame) {
                    jobs->markFailed(jobId, QStringLiteral("Local AST window timestamps did not map to a valid authoritative source-frame range."));
                    process->deleteLater();
                    return;
                }
                const int windowStartFrame = calculatedStart;
                const int windowEndFrame = calculatedEnd;
                const QJsonArray predictions = window.value(QStringLiteral("predictions")).toArray();
                if (predictions.size() > topK) {
                    jobs->markFailed(jobId, QStringLiteral("Local AST returned more ranked predictions than requested top_k."));
                    process->deleteLater();
                    return;
                }
                QSet<int> seenRanks;
                for (const QJsonValue &predictionValue : predictions) {
                    if (!predictionValue.isObject()) {
                        jobs->markFailed(jobId, QStringLiteral("Local AST returned a non-object prediction."));
                        process->deleteLater();
                        return;
                    }
                    const QJsonObject prediction = predictionValue.toObject();
                    const QString label = prediction.value(QStringLiteral("label")).toString().trimmed();
                    const QJsonValue labelIdValue = prediction.value(QStringLiteral("label_id"));
                    const double rawLabelId = labelIdValue.toDouble(-1.0);
                    const int labelId = labelIdValue.toInt(-1);
                    const QJsonValue rankValue = prediction.value(QStringLiteral("rank"));
                    const double rawRank = rankValue.toDouble(-1.0);
                    const int rank = rankValue.toInt(-1);
                    const double score = prediction.value(QStringLiteral("score")).toDouble(-1.0);
                    if (label.isEmpty() || label.size() > 256 || !labelIdValue.isDouble() || labelId < 0 ||
                        static_cast<double>(labelId) != rawLabelId || !rankValue.isDouble() || rank < 1 || rank > topK ||
                        static_cast<double>(rank) != rawRank || seenRanks.contains(rank) || !std::isfinite(score) ||
                        score < minScore - 0.000001 || score > 1.0) {
                        jobs->markFailed(jobId, QStringLiteral("Local AST returned an invalid ranked prediction."));
                        process->deleteLater();
                        return;
                    }
                    seenRanks.insert(rank);
                    VibeCutMediaEvidenceRecord record;
                    record.id = QStringLiteral("audio-event:%1:%2:%3")
                                    .arg(sourceId.mid(sourceId.indexOf(QLatin1Char(':')) + 1))
                                    .arg(sourceFingerprint.left(12))
                                    .arg(recordIndex++);
                    record.kind = QStringLiteral("audio_event_prediction");
                    record.startFrame = windowStartFrame;
                    record.endFrame = windowEndFrame;
                    record.text = QStringLiteral("AudioSet model prediction: %1 (score %2)")
                                      .arg(label).arg(score, 0, 'f', 4);
                    record.confidence = score;
                    record.metadata = QJsonObject{
                        {QStringLiteral("label"), label},
                        {QStringLiteral("label_id"), labelId},
                        {QStringLiteral("rank"), rank},
                        {QStringLiteral("window_start_frame"), windowStartFrame},
                        {QStringLiteral("window_end_frame"), windowEndFrame},
                        {QStringLiteral("window_index"), windowIndex},
                        {QStringLiteral("model"), kModel},
                        {QStringLiteral("taxonomy"), QStringLiteral("AudioSet")},
                        {QStringLiteral("authority"), QStringLiteral("model_prediction")},
                        {QStringLiteral("device"), root.value(QStringLiteral("device")).toString()},
                        {QStringLiteral("transformers_version"), rootTransformers},
                        {QStringLiteral("torch_version"), rootTorch},
                        {QStringLiteral("window_seconds"), root.value(QStringLiteral("window_seconds")).toDouble()},
                        {QStringLiteral("hop_seconds"), root.value(QStringLiteral("hop_seconds")).toDouble()},
                    };
                    records.append(record);
                }
            }

            QString persistError;
            if (!persistEvidence(sourceId, sourceFingerprint, kExtractorId, kExtractorVersion, records, &persistError)) {
                jobs->markFailed(jobId, QStringLiteral("Audio-event evidence was rejected: %1").arg(persistError));
                process->deleteLater();
                return;
            }
            jobs->markSucceeded(jobId, QStringLiteral("Persisted %1 ranked AudioSet prediction record(s) across %2 window(s).")
                                           .arg(records.size()).arg(windows.size()));
            process->deleteLater();
        });

        QObject::connect(process, &QProcess::errorOccurred, process,
                         [process, jobs = context.jobs, jobId](QProcess::ProcessError processError) {
                             if (processError != QProcess::FailedToStart) return;
                             jobs->markFailed(jobId, QStringLiteral("Could not launch the configured local AudioSet Python environment."));
                             process->deleteLater();
                         });

        process->start(vibeCutAudioEventPython(), arguments);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("started"), true},
                           {QStringLiteral("job_id"), jobId},
                           {QStringLiteral("model"), kModel},
                           {QStringLiteral("taxonomy"), QStringLiteral("AudioSet")},
                           {QStringLiteral("window_seconds"), windowSeconds},
                           {QStringLiteral("hop_seconds"), hopSeconds},
                           {QStringLiteral("required_windows"), requiredWindows},
                           {QStringLiteral("top_k"), topK},
                           {QStringLiteral("min_score"), minScore},
                           {QStringLiteral("device"), device}};
    }
};
}

QString vibeCutAudioEventVenvDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/vibecut-audio-events-venv");
}

QString vibeCutAudioEventPython()
{
    const QString overridePath = pythonOverride();
    return overridePath.isEmpty() ? vibeCutAudioEventVenvDir() + QStringLiteral("/bin/python3") : overridePath;
}

QString vibeCutAudioEventScript()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/vibecut/audio_events_ast.py"));
}

bool vibeCutAudioEventDependenciesReady(QString *error)
{
    if (error) error->clear();
    const QString script = vibeCutAudioEventScript();
    if (script.isEmpty() || !QFileInfo::exists(script)) {
        if (error) *error = QStringLiteral("VibeCut's installed AudioSet helper script was not found.");
        return false;
    }
    const QString python = vibeCutAudioEventPython();
    if (python.isEmpty() || !QFileInfo::exists(python)) {
        if (error) *error = QStringLiteral("Audio-event Python environment is missing. Run VibeCut audio-event setup first or set VIBECUT_AUDIO_EVENTS_PYTHON.");
        return false;
    }

    QProcess probe;
    probe.start(python, {QStringLiteral("-c"),
                         QStringLiteral("import transformers, torch, numpy; print(transformers.__version__); print(torch.__version__)")});
    if (!probe.waitForStarted(2000) || !probe.waitForFinished(7000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        if (error) *error = QStringLiteral("Configured Python cannot import the pinned Transformers/Torch audio-event runtime.");
        return false;
    }
    const QStringList lines = QString::fromUtf8(probe.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2 || lines.at(0).trimmed() != kTransformersVersion || !lines.at(1).trimmed().startsWith(kTorchVersion)) {
        if (error) *error = QStringLiteral("Audio-event runtime version mismatch: VibeCut requires transformers %1 and torch %2.x-compatible build for this adapter.")
                               .arg(kTransformersVersion, kTorchVersion);
        return false;
    }
    return true;
}

void ensureVibeCutLocalAudioEventProviderRegistered()
{
    static bool registered = false;
    if (registered) return;
    QString error;
    if (VibeCutExtractorProviderRegistry::global().registerProvider(kProviderId,
                                                                    []() { return std::make_unique<LocalAstAudioEventProvider>(); },
                                                                    &error)) {
        registered = true;
        return;
    }
    if (VibeCutExtractorProviderRegistry::global().providerIds().contains(kProviderId)) registered = true;
}
