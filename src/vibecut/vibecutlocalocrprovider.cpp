/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutlocalocrprovider.h"

#include "kdenlivesettings.h"
#include "vibecutextractorprovider.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtMath>

#include <memory>

namespace {
const QString kProviderId = QStringLiteral("local_tesseract");
const QString kExtractorId = QStringLiteral("local_tesseract_ocr");
const QString kAdapterVersion = QStringLiteral("1.0.0");

QString envOverride(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name)).trimmed();
}

QString engineVersionToken(const QString &version)
{
    QString token = version.trimmed().left(96);
    token.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return token.isEmpty() ? QStringLiteral("unknown") : token;
}

class LocalTesseractOcrProvider : public VibeCutExtractorProvider
{
public:
    QString id() const override { return kProviderId; }
    QString displayName() const override { return QStringLiteral("Local Tesseract OCR"); }
    QStringList capabilities() const override { return {QStringLiteral("ocr")}; }

    bool configured(QString *error) const override
    {
        if (error) error->clear();
        const QString python = vibeCutOcrPython();
        const QString script = vibeCutOcrScript();
        const QString tesseract = vibeCutTesseractExecutable();
        const QString ffmpeg = KdenliveSettings::ffmpegpath();
        if (python.isEmpty() || !QFileInfo::exists(python)) {
            if (error) *error = QStringLiteral("Local OCR requires python3 (or VIBECUT_OCR_PYTHON).");
            return false;
        }
        if (script.isEmpty() || !QFileInfo::exists(script)) {
            if (error) *error = QStringLiteral("VibeCut's installed Tesseract OCR helper script was not found.");
            return false;
        }
        if (tesseract.isEmpty() || !QFileInfo::exists(tesseract)) {
            if (error) *error = QStringLiteral("Local OCR requires the Tesseract executable (or VIBECUT_TESSERACT).");
            return false;
        }
        if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) {
            if (error) *error = QStringLiteral("Kdenlive has no valid configured FFmpeg executable for OCR frame sampling.");
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
        if (capability.trimmed().toLower() != QLatin1String("ocr")) {
            if (error) *error = QStringLiteral("Local Tesseract only implements OCR.");
            return QJsonObject();
        }
        if (!context.jobs || !context.persistEvidence) {
            if (error) *error = QStringLiteral("Local OCR requires the shared JobManager and validated evidence sink.");
            return QJsonObject();
        }
        if (!input.value(QStringLiteral("has_video")).toBool(false)) {
            if (error) *error = QStringLiteral("OCR requires a source with video.");
            return QJsonObject();
        }
        QString configuredError;
        if (!configured(&configuredError)) {
            if (error) *error = configuredError;
            return QJsonObject();
        }

        const QString sourcePath = input.value(QStringLiteral("source_path")).toString();
        const QString sourceId = input.value(QStringLiteral("source_id")).toString();
        const QString sourceFingerprint = input.value(QStringLiteral("source_fingerprint")).toString();
        const double fps = input.value(QStringLiteral("fps")).toDouble(0.0);
        const int startFrame = input.value(QStringLiteral("start_frame")).toInt(-1);
        const int endFrame = input.value(QStringLiteral("end_frame")).toInt(-1);
        if (sourcePath.isEmpty() || sourceId.isEmpty() || sourceFingerprint.isEmpty() || fps <= 0.0 || startFrame < 0 || endFrame <= startFrame) {
            if (error) *error = QStringLiteral("Local OCR received an incomplete normalized extractor request.");
            return QJsonObject();
        }

        const QJsonObject parameters = input.value(QStringLiteral("parameters")).toObject();
        const int defaultInterval = qMax(1, static_cast<int>(qRound64(fps)));
        const int sampleInterval = parameters.value(QStringLiteral("sample_interval_frames")).toInt(defaultInterval);
        const int maxSamples = parameters.value(QStringLiteral("max_samples")).toInt(300);
        const QString language = parameters.value(QStringLiteral("language")).toString(QStringLiteral("eng")).trimmed();
        const int psm = parameters.value(QStringLiteral("psm")).toInt(11);
        const double minConfidence = parameters.value(QStringLiteral("min_confidence")).toDouble(0.50);
        if (sampleInterval < 1 || sampleInterval > 1000000 || maxSamples < 1 || maxSamples > 2000 ||
            psm < 3 || psm > 13 || minConfidence < 0.0 || minConfidence > 1.0) {
            if (error) *error = QStringLiteral("OCR parameters are outside supported bounds.");
            return QJsonObject();
        }
        const QRegularExpression languagePattern(QStringLiteral("^[A-Za-z0-9_+.-]{1,128}$"));
        if (!languagePattern.match(language).hasMatch()) {
            if (error) *error = QStringLiteral("OCR language must use only Tesseract language-code characters [A-Za-z0-9_+.-].");
            return QJsonObject();
        }

        const QStringList arguments{
            vibeCutOcrScript(),
            QStringLiteral("--source"), sourcePath,
            QStringLiteral("--ffmpeg"), KdenliveSettings::ffmpegpath(),
            QStringLiteral("--tesseract"), vibeCutTesseractExecutable(),
            QStringLiteral("--fps"), QString::number(fps, 'f', 9),
            QStringLiteral("--start-frame"), QString::number(startFrame),
            QStringLiteral("--end-frame"), QString::number(endFrame),
            QStringLiteral("--sample-interval-frames"), QString::number(sampleInterval),
            QStringLiteral("--max-samples"), QString::number(maxSamples),
            QStringLiteral("--language"), language,
            QStringLiteral("--psm"), QString::number(psm),
            QStringLiteral("--min-confidence"), QString::number(minConfidence, 'f', 4),
        };

        const QString jobId = context.jobs->createJob(QStringLiteral("ocr"),
                                                      QStringLiteral("On-screen text OCR · %1").arg(sourceId),
                                                      true);
        context.jobs->markRunning(jobId, QStringLiteral("Sampling source frames and running local Tesseract OCR…"));

        auto *process = new QProcess(context.jobs);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(context.jobs, &VibeCutJobManager::jobChanged, process,
                         [jobs = context.jobs, process, jobId](const QString &changedId) {
                             if (changedId != jobId || process->state() == QProcess::NotRunning) return;
                             VibeCutJob job;
                             if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->terminate();
                         });

        const auto persistEvidence = context.persistEvidence;
        QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process,
                         [process, jobs = context.jobs, jobId, persistEvidence, sourceId, sourceFingerprint,
                          startFrame, endFrame, sampleInterval, language, psm](int exitCode, QProcess::ExitStatus exitStatus) {
                             VibeCutJob current;
                             if (jobs->job(jobId, current) && current.state == VibeCutJobState::CancelRequested) {
                                 jobs->markCancelled(jobId, QStringLiteral("OCR cancelled."));
                                 process->deleteLater();
                                 return;
                             }
                             if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                                 const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(5000).trimmed();
                                 jobs->markFailed(jobId, stderrText.isEmpty() ? QStringLiteral("Local OCR exited with code %1.").arg(exitCode) : stderrText);
                                 process->deleteLater();
                                 return;
                             }

                             const QByteArray stdoutData = process->readAllStandardOutput();
                             if (stdoutData.size() > 32 * 1024 * 1024) {
                                 jobs->markFailed(jobId, QStringLiteral("Local OCR output exceeded the 32 MiB safety limit."));
                                 process->deleteLater();
                                 return;
                             }
                             QJsonParseError parseError;
                             const QJsonDocument document = QJsonDocument::fromJson(stdoutData, &parseError);
                             if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                                 jobs->markFailed(jobId, QStringLiteral("Local OCR returned malformed JSON: %1").arg(parseError.errorString()));
                                 process->deleteLater();
                                 return;
                             }
                             const QJsonObject root = document.object();
                             if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 || !root.value(QStringLiteral("samples")).isArray()) {
                                 jobs->markFailed(jobId, QStringLiteral("Local OCR returned an unsupported result schema."));
                                 process->deleteLater();
                                 return;
                             }
                             const QString engine = root.value(QStringLiteral("engine")).toString().trimmed();
                             const QString engineVersion = root.value(QStringLiteral("engine_version")).toString().trimmed();
                             if (engine.isEmpty() || engineVersion.isEmpty()) {
                                 jobs->markFailed(jobId, QStringLiteral("Local OCR omitted engine provenance."));
                                 process->deleteLater();
                                 return;
                             }
                             const QString extractorVersion = QStringLiteral("%1/%2").arg(kAdapterVersion, engineVersionToken(engineVersion));

                             QList<VibeCutMediaEvidenceRecord> records;
                             int recordIndex = 0;
                             for (const QJsonValue &sampleValue : root.value(QStringLiteral("samples")).toArray()) {
                                 const QJsonObject sample = sampleValue.toObject();
                                 const int frame = sample.value(QStringLiteral("frame")).toInt(-1);
                                 const int imageWidth = sample.value(QStringLiteral("image_width")).toInt(-1);
                                 const int imageHeight = sample.value(QStringLiteral("image_height")).toInt(-1);
                                 if (frame < startFrame || frame >= endFrame || imageWidth <= 0 || imageHeight <= 0) {
                                     jobs->markFailed(jobId, QStringLiteral("Local OCR returned a sample outside authoritative bounds or with invalid dimensions."));
                                     process->deleteLater();
                                     return;
                                 }
                                 for (const QJsonValue &lineValue : sample.value(QStringLiteral("lines")).toArray()) {
                                     const QJsonObject line = lineValue.toObject();
                                     const QString text = line.value(QStringLiteral("text")).toString().trimmed();
                                     const double confidence = line.value(QStringLiteral("confidence")).toDouble(-1.0);
                                     const QJsonObject box = line.value(QStringLiteral("bbox_pixels")).toObject();
                                     if (text.isEmpty()) continue;
                                     VibeCutMediaEvidenceRecord record;
                                     record.id = QStringLiteral("ocr:%1:%2:%3").arg(sourceFingerprint.left(12)).arg(frame).arg(recordIndex++);
                                     record.kind = QStringLiteral("ocr_text");
                                     record.startFrame = frame;
                                     record.endFrame = frame + 1;
                                     record.text = text.left(4096);
                                     record.confidence = confidence;
                                     record.metadata = QJsonObject{{QStringLiteral("sample_frame"), frame},
                                                                   {QStringLiteral("image_width"), imageWidth},
                                                                   {QStringLiteral("image_height"), imageHeight},
                                                                   {QStringLiteral("bbox_pixels"), box},
                                                                   {QStringLiteral("language"), language},
                                                                   {QStringLiteral("engine"), engine},
                                                                   {QStringLiteral("engine_version"), engineVersion},
                                                                   {QStringLiteral("psm"), psm},
                                                                   {QStringLiteral("sample_interval_frames"), sampleInterval}};
                                     records.append(record);
                                 }
                             }

                             QString persistError;
                             if (!persistEvidence(sourceId, sourceFingerprint, kExtractorId, extractorVersion, records, &persistError)) {
                                 jobs->markFailed(jobId, QStringLiteral("OCR evidence was rejected: %1").arg(persistError));
                                 process->deleteLater();
                                 return;
                             }
                             jobs->markSucceeded(jobId, QStringLiteral("OCR persisted %1 on-screen text observation(s).").arg(records.size()));
                             process->deleteLater();
                         });

        QObject::connect(process, &QProcess::errorOccurred, process,
                         [process, jobs = context.jobs, jobId](QProcess::ProcessError processError) {
                             if (processError != QProcess::FailedToStart) return;
                             jobs->markFailed(jobId, QStringLiteral("Could not launch the configured local OCR Python helper."));
                             process->deleteLater();
                         });

        process->start(vibeCutOcrPython(), arguments);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("started"), true},
                           {QStringLiteral("job_id"), jobId},
                           {QStringLiteral("sample_interval_frames"), sampleInterval},
                           {QStringLiteral("max_samples"), maxSamples},
                           {QStringLiteral("language"), language},
                           {QStringLiteral("psm"), psm},
                           {QStringLiteral("min_confidence"), minConfidence}};
    }
};
}

QString vibeCutTesseractExecutable()
{
    const QString overridePath = envOverride("VIBECUT_TESSERACT");
    return overridePath.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("tesseract")) : overridePath;
}

QString vibeCutOcrPython()
{
    const QString overridePath = envOverride("VIBECUT_OCR_PYTHON");
    if (!overridePath.isEmpty()) return overridePath;
    QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
    return python;
}

QString vibeCutOcrScript()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/vibecut/ocr_tesseract.py"));
}

void ensureVibeCutLocalOcrProviderRegistered()
{
    static bool registered = false;
    if (registered) return;
    QString error;
    if (VibeCutExtractorProviderRegistry::global().registerProvider(kProviderId,
                                                                    []() { return std::make_unique<LocalTesseractOcrProvider>(); },
                                                                    &error)) {
        registered = true;
        return;
    }
    if (VibeCutExtractorProviderRegistry::global().providerIds().contains(kProviderId)) registered = true;
}
