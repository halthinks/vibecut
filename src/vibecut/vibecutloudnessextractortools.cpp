/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutloudnessextractortools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace {
constexpr auto ExtractorId = "loudness_detect";
constexpr auto ExtractorVersion = "1.0.0";

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' + QByteArray::number(info.size()) + '\n' + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

bool parseMetric(const QString &text, const QString &label, double &value)
{
    const QRegularExpression expression(QStringLiteral("%1:\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*dB").arg(QRegularExpression::escape(label)));
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch()) return false;
    value = match.captured(1).toDouble();
    return true;
}

QJsonObject startLoudness(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    QString persistError;
    if (!VibeCutMediaEvidence::canPersistCurrent(&persistError)) return err(persistError);
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const double clippingThresholdDb = qBound(-6.0, input.value(QStringLiteral("clipping_threshold_db")).toDouble(-0.1), 0.0);
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Loudness detection requires a file-backed source."));
    if (!clip->hasAudio()) return err(QStringLiteral("Bin clip '%1' has no audio stream.").arg(binId));
    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Source file is missing or invalid."));
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) return err(QStringLiteral("Kdenlive has no valid configured FFmpeg executable."));
    const QString fingerprint = statFingerprint(info);
    VibeCutJobManager *jobs = tools->jobManager();
    if (!jobs) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString jobId = jobs->createJob(QStringLiteral("media_loudness"), QStringLiteral("Measure loudness in %1").arg(info.fileName()), true);
    jobs->markRunning(jobId, QStringLiteral("FFmpeg volumedetect is starting."));
    QProcess *process = new QProcess(tools);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), info.absoluteFilePath(), QStringLiteral("-af"), QStringLiteral("volumedetect"), QStringLiteral("-vn"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process, [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->kill();
    });
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), tools,
                     [jobs, process, jobId, binId, fingerprint, clippingThresholdDb, durationFrames = clip->getFramePlaytime()](int exitCode, QProcess::ExitStatus exitStatus) {
        VibeCutJob job;
        jobs->job(jobId, job);
        if (job.state == VibeCutJobState::CancelRequested) {
            jobs->markCancelled(jobId, QStringLiteral("Loudness detection cancelled."));
            process->deleteLater();
            return;
        }
        const QString stderrText = QString::fromUtf8(process->readAllStandardError());
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            jobs->markFailed(jobId, QStringLiteral("FFmpeg volumedetect failed (exit %1).").arg(exitCode));
            process->deleteLater();
            return;
        }
        double meanVolume = 0.0;
        double maxVolume = 0.0;
        if (!parseMetric(stderrText, QStringLiteral("mean_volume"), meanVolume) || !parseMetric(stderrText, QStringLiteral("max_volume"), maxVolume)) {
            jobs->markFailed(jobId, QStringLiteral("FFmpeg completed but mean/max volume metrics could not be parsed."));
            process->deleteLater();
            return;
        }
        const bool nearClipping = maxVolume >= clippingThresholdDb;
        VibeCutMediaEvidenceRecord record;
        record.id = QStringLiteral("loudness:%1:%2").arg(binId, fingerprint.left(16));
        record.sourceId = QStringLiteral("bin:%1").arg(binId);
        record.sourceFingerprint = fingerprint;
        record.extractorId = QString::fromLatin1(ExtractorId);
        record.extractorVersion = QString::fromLatin1(ExtractorVersion);
        record.kind = QStringLiteral("loudness_summary");
        record.startFrame = 0;
        record.endFrame = qMax(0, durationFrames);
        record.text = QStringLiteral("audio loudness mean %1 dB max %2 dB %3").arg(meanVolume, 0, 'f', 2).arg(maxVolume, 0, 'f', 2).arg(nearClipping ? QStringLiteral("near clipping") : QStringLiteral("not clipping"));
        record.confidence = 1.0;
        record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        record.metadata = QJsonObject{{QStringLiteral("mean_volume_db"), meanVolume}, {QStringLiteral("max_volume_db"), maxVolume}, {QStringLiteral("clipping_threshold_db"), clippingThresholdDb}, {QStringLiteral("near_clipping"), nearClipping}};
        QString error;
        if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(record.sourceId, fingerprint, record.extractorId, record.extractorVersion, QList<VibeCutMediaEvidenceRecord>{record}, &error)) {
            jobs->markFailed(jobId, QStringLiteral("Loudness evidence persistence failed: %1").arg(error));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Measured mean %1 dB, max %2 dB%3.").arg(meanVolume, 0, 'f', 2).arg(maxVolume, 0, 'f', 2).arg(nearClipping ? QStringLiteral(" (near clipping)") : QString()));
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, tools, [jobs, process, jobId](QProcess::ProcessError processError) {
        if (processError == QProcess::Crashed) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && !job.terminal()) jobs->markFailed(jobId, QStringLiteral("Could not start/run FFmpeg volumedetect: %1").arg(process->errorString()));
    });
    process->start(ffmpeg, args);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job_id"), jobId}, {QStringLiteral("bin_id"), binId}, {QStringLiteral("source_fingerprint"), fingerprint}, {QStringLiteral("extractor_id"), QString::fromLatin1(ExtractorId)}, {QStringLiteral("extractor_version"), QString::fromLatin1(ExtractorVersion)}, {QStringLiteral("clipping_threshold_db"), clippingThresholdDb}, {QStringLiteral("asynchronous"), true}};
}
} // namespace

bool registerVibeCutLoudnessExtractorTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Loudness extractor requires the native VibeCutTools/JobManager surface.");
        return false;
    }
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}, {QStringLiteral("clipping_threshold_db"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), -6}, {QStringLiteral("maximum"), 0}}}}}, {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}}, {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_loudness_refresh")}, {QStringLiteral("description"), QStringLiteral("Asynchronously run Kdenlive's configured FFmpeg volumedetect on one file-backed audio/video bin asset, then persist measured mean/max volume and near-clipping status as versioned extractor evidence. Returns a shared VibeCut job id and supports job_cancel.")}, {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_loudness_refresh");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    return surface.registerTool(schema, policy, [tools](const QJsonObject &input) { return startLoudness(tools, input); }, error);
}
