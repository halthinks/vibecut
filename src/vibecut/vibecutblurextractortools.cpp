/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutblurextractortools.h"

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
constexpr auto ExtractorId = "blur_detect";
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

QJsonObject startBlur(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    QString persistError;
    if (!VibeCutMediaEvidence::canPersistCurrent(&persistError)) return err(persistError);
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Blur detection requires a file-backed source."));
    if (!clip->hasVideo()) return err(QStringLiteral("Bin clip '%1' has no video stream.").arg(binId));
    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Source file is missing or invalid."));
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) return err(QStringLiteral("Kdenlive has no valid configured FFmpeg executable."));
    const QString fingerprint = statFingerprint(info);

    VibeCutJobManager *jobs = tools->jobManager();
    if (!jobs) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString jobId = jobs->createJob(QStringLiteral("media_blur"), QStringLiteral("Measure blur in %1").arg(info.fileName()), true);
    jobs->markRunning(jobId, QStringLiteral("FFmpeg blurdetect is starting."));
    QProcess *process = new QProcess(tools);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), info.absoluteFilePath(),
                           QStringLiteral("-vf"), QStringLiteral("blurdetect"), QStringLiteral("-an"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};

    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process, [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->kill();
    });

    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), tools,
                     [jobs, process, jobId, binId, fingerprint, durationFrames = clip->getFramePlaytime()](int exitCode, QProcess::ExitStatus exitStatus) {
        VibeCutJob job;
        jobs->job(jobId, job);
        if (job.state == VibeCutJobState::CancelRequested) {
            jobs->markCancelled(jobId, QStringLiteral("Blur detection cancelled.")); process->deleteLater(); return;
        }
        const QString stderrText = QString::fromUtf8(process->readAllStandardError());
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            jobs->markFailed(jobId, QStringLiteral("FFmpeg blurdetect failed (exit %1).").arg(exitCode)); process->deleteLater(); return;
        }
        const QRegularExpression re(QStringLiteral("blur mean:\\s*([0-9]+(?:\\.[0-9]+)?)"));
        const QRegularExpressionMatch match = re.match(stderrText);
        if (!match.hasMatch()) {
            jobs->markFailed(jobId, QStringLiteral("FFmpeg completed but blur mean could not be parsed; the installed FFmpeg may not support blurdetect."));
            process->deleteLater(); return;
        }
        const double blurMean = match.captured(1).toDouble();
        VibeCutMediaEvidenceRecord record;
        record.id = QStringLiteral("blur:%1:%2").arg(binId, fingerprint.left(16));
        record.sourceId = QStringLiteral("bin:%1").arg(binId);
        record.sourceFingerprint = fingerprint;
        record.extractorId = QString::fromLatin1(ExtractorId);
        record.extractorVersion = QString::fromLatin1(ExtractorVersion);
        record.kind = QStringLiteral("blur_summary");
        record.startFrame = 0;
        record.endFrame = qMax(0, durationFrames);
        record.text = QStringLiteral("visual blur mean %1").arg(blurMean, 0, 'f', 7);
        record.confidence = 1.0;
        record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        record.metadata = QJsonObject{{QStringLiteral("blur_mean"), blurMean},
                                      {QStringLiteral("metric"), QStringLiteral("FFmpeg lavfi.blur / blurdetect mean")},
                                      {QStringLiteral("classification"), QStringLiteral("unclassified_raw_metric")}};
        QString error;
        if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(record.sourceId, fingerprint, record.extractorId, record.extractorVersion,
                                                                  QList<VibeCutMediaEvidenceRecord>{record}, &error)) {
            jobs->markFailed(jobId, QStringLiteral("Blur evidence persistence failed: %1").arg(error)); process->deleteLater(); return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Measured blur mean %1 and persisted evidence.").arg(blurMean, 0, 'f', 7));
        process->deleteLater();
    });

    QObject::connect(process, &QProcess::errorOccurred, tools, [jobs, process, jobId](QProcess::ProcessError processError) {
        if (processError == QProcess::Crashed) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && !job.terminal()) jobs->markFailed(jobId, QStringLiteral("Could not start/run FFmpeg blurdetect: %1").arg(process->errorString()));
    });
    process->start(ffmpeg, args);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job_id"), jobId}, {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("source_fingerprint"), fingerprint}, {QStringLiteral("extractor_id"), QString::fromLatin1(ExtractorId)},
                       {QStringLiteral("extractor_version"), QString::fromLatin1(ExtractorVersion)}, {QStringLiteral("asynchronous"), true}};
}
} // namespace

bool registerVibeCutBlurExtractorTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) { if (error) *error = QStringLiteral("Blur extractor requires the native VibeCutTools/JobManager surface."); return false; }
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}}, {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_blur_refresh")},
                             {QStringLiteral("description"), QStringLiteral("Asynchronously run FFmpeg blurdetect using Kdenlive's configured FFmpeg and persist the raw source-wide blur mean as versioned evidence. No arbitrary good/bad threshold is imposed; evaluation/editorial policy can interpret the metric later.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_blur_refresh"); policy.risk = VibeCutToolRisk::ExternalSideEffect; policy.asynchronous = true; policy.mutatesProject = false;
    return surface.registerTool(schema, policy, [tools](const QJsonObject &input) { return startBlur(tools, input); }, error);
}
