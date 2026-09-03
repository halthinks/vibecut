/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutlocalobjectprovider.h"

#include "kdenlivesettings.h"
#include "vibecutextractorprovider.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"
#include "vibecutvisionruntime.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QtMath>

#include <cmath>
#include <limits>
#include <memory>

namespace {
const QString kProviderId = QStringLiteral("local_detr_coco");
const QString kExtractorId = QStringLiteral("local_detr_coco");
const QString kExtractorVersion = QStringLiteral("1.0.0");
const QString kModel = QStringLiteral("facebook/detr-resnet-50");
const QString kModelRevision = QStringLiteral("ebd66332d81f2ee6d9fbfefd0235026b46a381d0");
const QString kTaxonomy = QStringLiteral("COCO-2017");
const QString kTransformersVersion = QStringLiteral("5.16.1");
const QString kTorchVersion = QStringLiteral("2.14.0");
const QString kTorchvisionVersion = QStringLiteral("0.29.0");
constexpr int MaxSamples = 1000;

bool readBoundedDouble(const QJsonObject &parameters, const QString &name, double fallback,
                       double minimum, double maximum, double &result, QString *error)
{
    if (!parameters.contains(name)) { result = fallback; return true; }
    const QJsonValue value = parameters.value(name);
    if (!value.isDouble()) { if (error) *error = QStringLiteral("Object-detection parameter %1 must be numeric.").arg(name); return false; }
    result = value.toDouble();
    if (!std::isfinite(result) || result < minimum || result > maximum) {
        if (error) *error = QStringLiteral("Object-detection parameter %1 must be between %2 and %3.").arg(name).arg(minimum).arg(maximum);
        return false;
    }
    return true;
}

bool readBoundedInt(const QJsonObject &parameters, const QString &name, int fallback,
                    int minimum, int maximum, int &result, QString *error)
{
    if (!parameters.contains(name)) { result = fallback; return true; }
    const QJsonValue value = parameters.value(name);
    const double raw = value.toDouble(static_cast<double>(minimum - 1));
    const int converted = value.toInt(minimum - 1);
    if (!value.isDouble() || !std::isfinite(raw) || static_cast<double>(converted) != raw || converted < minimum || converted > maximum) {
        if (error) *error = QStringLiteral("Object-detection parameter %1 must be an integer from %2 to %3.").arg(name).arg(minimum).arg(maximum);
        return false;
    }
    result = converted;
    return true;
}

class LocalDetrObjectProvider : public VibeCutExtractorProvider
{
public:
    QString id() const override { return kProviderId; }
    QString displayName() const override { return QStringLiteral("Local DETR COCO object detector"); }
    QStringList capabilities() const override { return {QStringLiteral("objects")}; }

    bool configured(QString *error) const override
    {
        if (!vibeCutVisionDependenciesReady(error)) return false;
        const QString script = vibeCutObjectDetectionScript();
        if (script.isEmpty() || !QFileInfo::exists(script)) {
            if (error) *error = QStringLiteral("VibeCut's installed DETR object-detection helper was not found.");
            return false;
        }
        return true;
    }

    QJsonObject start(const QString &capability, const QJsonObject &input,
                      const VibeCutExtractorProviderContext &context, QString *error) override
    {
        if (error) error->clear();
        if (capability.trimmed().toLower() != QLatin1String("objects")) { if (error) *error = QStringLiteral("Local DETR only implements objects."); return QJsonObject(); }
        if (!context.jobs || !context.persistEvidence) { if (error) *error = QStringLiteral("Local DETR requires the shared JobManager and validated evidence sink."); return QJsonObject(); }
        if (!input.value(QStringLiteral("has_video")).toBool(false)) { if (error) *error = QStringLiteral("Object detection requires a source with video."); return QJsonObject(); }
        QString readyError;
        if (!configured(&readyError)) { if (error) *error = readyError; return QJsonObject(); }

        const QString sourcePath = input.value(QStringLiteral("source_path")).toString();
        const QString sourceId = input.value(QStringLiteral("source_id")).toString();
        const QString sourceFingerprint = input.value(QStringLiteral("source_fingerprint")).toString();
        const int startFrame = input.value(QStringLiteral("start_frame")).toInt(-1);
        const int endFrame = input.value(QStringLiteral("end_frame")).toInt(-1);
        if (sourcePath.isEmpty() || sourceId.isEmpty() || sourceFingerprint.isEmpty() || startFrame < 0 || endFrame <= startFrame) {
            if (error) *error = QStringLiteral("Local DETR received an incomplete normalized extractor request.");
            return QJsonObject();
        }
        const QString ffmpeg = KdenliveSettings::ffmpegpath();
        if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) { if (error) *error = QStringLiteral("Kdenlive has no valid configured FFmpeg executable for visual sampling."); return QJsonObject(); }

        const QJsonObject parameters = input.value(QStringLiteral("parameters")).toObject();
        if ((parameters.contains(QStringLiteral("model")) && parameters.value(QStringLiteral("model")).toString() != kModel) ||
            (parameters.contains(QStringLiteral("revision")) && parameters.value(QStringLiteral("revision")).toString() != kModelRevision)) {
            if (error) *error = QStringLiteral("The built-in DETR provider is pinned to %1@%2.").arg(kModel, kModelRevision);
            return QJsonObject();
        }
        int sampleIntervalFrames = 30, maxSamples = 300, maxDetections = 50;
        double minScore = 0.70;
        if (!readBoundedInt(parameters, QStringLiteral("sample_interval_frames"), 30, 1, 1000000, sampleIntervalFrames, error) ||
            !readBoundedInt(parameters, QStringLiteral("max_samples"), 300, 1, MaxSamples, maxSamples, error) ||
            !readBoundedInt(parameters, QStringLiteral("max_detections_per_frame"), 50, 1, 100, maxDetections, error) ||
            !readBoundedDouble(parameters, QStringLiteral("min_score"), 0.70, 0.0, 1.0, minScore, error)) return QJsonObject();
        const QString device = parameters.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
        if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) { if (error) *error = QStringLiteral("Object-detection parameter device must be auto, cpu, or cuda."); return QJsonObject(); }
        const qint64 requiredSamples = 1 + (static_cast<qint64>(endFrame) - startFrame - 1) / sampleIntervalFrames;
        if (requiredSamples < 1 || requiredSamples > maxSamples) { if (error) *error = QStringLiteral("Object-detection range/cadence requires %1 sampled frames, exceeding max_samples=%2; increase sample_interval_frames or use a smaller range.").arg(requiredSamples).arg(maxSamples); return QJsonObject(); }

        const QStringList arguments{vibeCutObjectDetectionScript(), QStringLiteral("--source"), sourcePath, QStringLiteral("--ffmpeg"), ffmpeg,
                                    QStringLiteral("--start-frame"), QString::number(startFrame), QStringLiteral("--end-frame"), QString::number(endFrame),
                                    QStringLiteral("--sample-interval-frames"), QString::number(sampleIntervalFrames), QStringLiteral("--max-samples"), QString::number(maxSamples),
                                    QStringLiteral("--min-score"), QString::number(minScore, 'f', 6), QStringLiteral("--max-detections-per-frame"), QString::number(maxDetections),
                                    QStringLiteral("--device"), device, QStringLiteral("--model"), kModel, QStringLiteral("--revision"), kModelRevision};

        const QString jobId = context.jobs->createJob(QStringLiteral("visual_objects"), QStringLiteral("Object detection · %1").arg(sourceId), true);
        context.jobs->markRunning(jobId, QStringLiteral("Running local DETR object detection…"));
        auto *process = new QProcess(context.jobs);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"), QStringLiteral("1"));
        environment.insert(QStringLiteral("DO_NOT_TRACK"), QStringLiteral("1"));
        process->setProcessEnvironment(environment);
        QObject::connect(context.jobs, &VibeCutJobManager::jobChanged, process, [jobs = context.jobs, process, jobId](const QString &changedId) {
            if (changedId != jobId || process->state() == QProcess::NotRunning) return;
            VibeCutJob job; if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->terminate();
        });

        const auto persistEvidence = context.persistEvidence;
        QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                         [process, jobs = context.jobs, jobId, persistEvidence, sourceId, sourceFingerprint, startFrame, endFrame,
                          sampleIntervalFrames, requiredSamples, maxDetections, minScore](int exitCode, QProcess::ExitStatus status) {
            VibeCutJob current;
            if (jobs->job(jobId, current) && current.state == VibeCutJobState::CancelRequested) { jobs->markCancelled(jobId, QStringLiteral("Object detection cancelled.")); process->deleteLater(); return; }
            if (status != QProcess::NormalExit || exitCode != 0) { const QString e = QString::fromUtf8(process->readAllStandardError()).right(6000).trimmed(); jobs->markFailed(jobId, e.isEmpty() ? QStringLiteral("Local DETR exited with code %1.").arg(exitCode) : e); process->deleteLater(); return; }
            const QByteArray stdoutData = process->readAllStandardOutput();
            if (stdoutData.size() > 64 * 1024 * 1024) { jobs->markFailed(jobId, QStringLiteral("Local DETR output exceeded the 64 MiB safety limit.")); process->deleteLater(); return; }
            QJsonParseError parseError; const QJsonDocument document = QJsonDocument::fromJson(stdoutData, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) { jobs->markFailed(jobId, QStringLiteral("Local DETR returned malformed JSON: %1").arg(parseError.errorString())); process->deleteLater(); return; }
            const QJsonObject root = document.object();
            const QString transformersVersion = root.value(QStringLiteral("transformers_version")).toString().trimmed();
            const QString torchVersion = root.value(QStringLiteral("torch_version")).toString().trimmed();
            const QString torchvisionVersion = root.value(QStringLiteral("torchvision_version")).toString().trimmed();
            if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 || root.value(QStringLiteral("authority")).toString() != QLatin1String("model_prediction") ||
                root.value(QStringLiteral("taxonomy")).toString() != kTaxonomy || root.value(QStringLiteral("model")).toString() != kModel ||
                root.value(QStringLiteral("model_revision")).toString() != kModelRevision || transformersVersion != kTransformersVersion || !torchVersion.startsWith(kTorchVersion) ||
                !torchvisionVersion.startsWith(kTorchvisionVersion) || root.value(QStringLiteral("sample_interval_frames")).toInt(-1) != sampleIntervalFrames ||
                root.value(QStringLiteral("sample_count")).toInt(-1) != requiredSamples || !root.value(QStringLiteral("samples")).isArray()) {
                jobs->markFailed(jobId, QStringLiteral("Local DETR returned an unsupported or provenance-mismatched result schema.")); process->deleteLater(); return;
            }
            const QJsonArray samples = root.value(QStringLiteral("samples")).toArray();
            if (samples.size() != requiredSamples) { jobs->markFailed(jobId, QStringLiteral("Local DETR result did not cover the exact requested sample sequence.")); process->deleteLater(); return; }
            QList<VibeCutMediaEvidenceRecord> records; int recordIndex = 0;
            for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
                if (!samples.at(sampleIndex).isObject()) { jobs->markFailed(jobId, QStringLiteral("Local DETR returned a non-object sample.")); process->deleteLater(); return; }
                const QJsonObject sample = samples.at(sampleIndex).toObject();
                const qint64 expectedFrame64 = static_cast<qint64>(startFrame) + static_cast<qint64>(sampleIndex) * sampleIntervalFrames;
                if (expectedFrame64 < startFrame || expectedFrame64 >= endFrame || expectedFrame64 > std::numeric_limits<int>::max()) { jobs->markFailed(jobId, QStringLiteral("Internal DETR sample-frame calculation overflowed authoritative bounds.")); process->deleteLater(); return; }
                const int expectedFrame = static_cast<int>(expectedFrame64);
                const int frame = sample.value(QStringLiteral("frame")).toInt(-1), imageWidth = sample.value(QStringLiteral("image_width")).toInt(-1), imageHeight = sample.value(QStringLiteral("image_height")).toInt(-1);
                const QJsonArray detections = sample.value(QStringLiteral("detections")).toArray();
                if (frame != expectedFrame || imageWidth <= 0 || imageHeight <= 0 || detections.size() > maxDetections) { jobs->markFailed(jobId, QStringLiteral("Local DETR returned invalid sampled-frame metadata.")); process->deleteLater(); return; }
                for (const QJsonValue &detectionValue : detections) {
                    if (!detectionValue.isObject()) { jobs->markFailed(jobId, QStringLiteral("Local DETR returned a non-object detection.")); process->deleteLater(); return; }
                    const QJsonObject detection = detectionValue.toObject(); const QString label = detection.value(QStringLiteral("label")).toString().trimmed();
                    const QJsonValue labelIdValue = detection.value(QStringLiteral("label_id")); const double rawLabelId = labelIdValue.toDouble(-1.0); const int labelId = labelIdValue.toInt(-1);
                    const double score = detection.value(QStringLiteral("score")).toDouble(-1.0); const QJsonObject box = detection.value(QStringLiteral("bbox_pixels")).toObject();
                    const int x = box.value(QStringLiteral("x")).toInt(-1), y = box.value(QStringLiteral("y")).toInt(-1), width = box.value(QStringLiteral("width")).toInt(-1), height = box.value(QStringLiteral("height")).toInt(-1);
                    if (label.isEmpty() || label.size() > 256 || !labelIdValue.isDouble() || labelId < 0 || static_cast<double>(labelId) != rawLabelId || !std::isfinite(score) || score < minScore - 0.000001 || score > 1.0 ||
                        x < 0 || y < 0 || width <= 0 || height <= 0 || static_cast<qint64>(x) + width > imageWidth || static_cast<qint64>(y) + height > imageHeight) {
                        jobs->markFailed(jobId, QStringLiteral("Local DETR returned an invalid detection record.")); process->deleteLater(); return;
                    }
                    VibeCutMediaEvidenceRecord record;
                    record.id = QStringLiteral("object:%1:%2:%3").arg(sourceId.mid(sourceId.indexOf(QLatin1Char(':')) + 1), sourceFingerprint.left(12)).arg(recordIndex++);
                    record.sourceId = sourceId; record.sourceFingerprint = sourceFingerprint; record.extractorId = kExtractorId; record.extractorVersion = kExtractorVersion;
                    record.kind = QStringLiteral("object_detection_prediction"); record.startFrame = frame; record.endFrame = frame + 1;
                    record.text = QStringLiteral("COCO object-model prediction: %1 (score %2)").arg(label).arg(score, 0, 'f', 4); record.confidence = score;
                    record.metadata = QJsonObject{{QStringLiteral("sample_frame"), frame}, {QStringLiteral("image_width"), imageWidth}, {QStringLiteral("image_height"), imageHeight},
                                                  {QStringLiteral("bbox_pixels"), box}, {QStringLiteral("label"), label}, {QStringLiteral("label_id"), labelId}, {QStringLiteral("model"), kModel},
                                                  {QStringLiteral("model_revision"), kModelRevision}, {QStringLiteral("taxonomy"), kTaxonomy}, {QStringLiteral("authority"), QStringLiteral("model_prediction")},
                                                  {QStringLiteral("device"), root.value(QStringLiteral("device")).toString()}, {QStringLiteral("transformers_version"), transformersVersion},
                                                  {QStringLiteral("torch_version"), torchVersion}, {QStringLiteral("torchvision_version"), torchvisionVersion},
                                                  {QStringLiteral("sample_interval_frames"), sampleIntervalFrames}};
                    records.append(record);
                }
            }
            QString persistError;
            if (!persistEvidence(sourceId, sourceFingerprint, kExtractorId, kExtractorVersion, records, &persistError)) { jobs->markFailed(jobId, QStringLiteral("Object-detection evidence was rejected: %1").arg(persistError)); process->deleteLater(); return; }
            jobs->markSucceeded(jobId, QStringLiteral("Persisted %1 object-model prediction(s) across %2 sampled frame(s).").arg(records.size()).arg(samples.size())); process->deleteLater();
        });
        QObject::connect(process, &QProcess::errorOccurred, process, [process, jobs = context.jobs, jobId](QProcess::ProcessError processError) {
            if (processError != QProcess::FailedToStart) return; jobs->markFailed(jobId, QStringLiteral("Could not launch the configured VibeCut vision Python environment.")); process->deleteLater();
        });
        process->start(vibeCutVisionPython(), arguments);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true}, {QStringLiteral("job_id"), jobId}, {QStringLiteral("model"), kModel},
                           {QStringLiteral("model_revision"), kModelRevision}, {QStringLiteral("taxonomy"), kTaxonomy}, {QStringLiteral("required_samples"), requiredSamples},
                           {QStringLiteral("sample_interval_frames"), sampleIntervalFrames}, {QStringLiteral("min_score"), minScore}, {QStringLiteral("device"), device}};
    }
};
}

QString vibeCutObjectDetectionScript()
{
    // Keep this path exactly aligned with data/scripts/vibecut/CMakeLists.txt.
    return QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/vibecut/objects_detr.py"));
}

void ensureVibeCutLocalObjectProviderRegistered()
{
    static bool registered = false;
    if (registered) return;
    QString error;
    if (VibeCutExtractorProviderRegistry::global().registerProvider(kProviderId, []() { return std::make_unique<LocalDetrObjectProvider>(); }, &error)) { registered = true; return; }
    if (VibeCutExtractorProviderRegistry::global().providerIds().contains(kProviderId)) registered = true;
}
