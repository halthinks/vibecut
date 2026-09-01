/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutfreezeextractortools.h"

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
constexpr auto ExtractorId = "freeze_detect";
constexpr auto ExtractorVersion = "1.0.0";

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' +
                               QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QList<QPair<double, double>> parseFreezeRanges(const QString &output)
{
    QList<QPair<double, double>> ranges;
    const QRegularExpression startRe(QStringLiteral("freeze_start:\\s*([0-9]+(?:\\.[0-9]+)?)"));
    const QRegularExpression endRe(QStringLiteral("freeze_end:\\s*([0-9]+(?:\\.[0-9]+)?)"));
    double pending = -1.0;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch startMatch = startRe.match(line);
        if (startMatch.hasMatch()) {
            pending = startMatch.captured(1).toDouble();
            continue;
        }
        const QRegularExpressionMatch endMatch = endRe.match(line);
        if (endMatch.hasMatch() && pending >= 0.0) {
            const double end = endMatch.captured(1).toDouble();
            if (end >= pending) ranges.append({pending, end});
            pending = -1.0;
        }
    }
    return ranges;
}

QJsonObject startFreeze(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const int minDurationMs = qBound(100, input.value(QStringLiteral("min_duration_ms")).toInt(2000), 600000);
    const double noiseDb = qBound(-100.0, input.value(QStringLiteral("noise_db")).toDouble(-60.0), 0.0);

    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Freeze detection requires a file-backed source."));
    if (!clip->hasVideo()) return err(QStringLiteral("Bin clip '%1' has no video stream.").arg(binId));

    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Source file is missing or invalid."));
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) return err(QStringLiteral("Kdenlive has no valid configured FFmpeg executable."));
    const QString fingerprint = statFingerprint(info);
    const double fps = pCore->getCurrentFps();
    if (fps <= 0.0) return err(QStringLiteral("Current project frame rate is invalid."));

    VibeCutJobManager *jobs = tools->jobManager();
    if (!jobs) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString jobId = jobs->createJob(QStringLiteral("media_freeze"), QStringLiteral("Detect frozen video in %1").arg(info.fileName()), true);
    jobs->markRunning(jobId, QStringLiteral("FFmpeg freezedetect is starting."));

    QProcess *process = new QProcess(tools);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const QString filter = QStringLiteral("freezedetect=n=%1dB:d=%2")
                               .arg(noiseDb, 0, 'f', 1)
                               .arg(minDurationMs / 1000.0, 0, 'f', 3);
    const QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostats"),
                           QStringLiteral("-i"), info.absoluteFilePath(),
                           QStringLiteral("-vf"), filter,
                           QStringLiteral("-an"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};

    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process, [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->kill();
    });

    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), tools,
                     [jobs, process, jobId, binId, fingerprint, fps, minDurationMs, noiseDb](int exitCode, QProcess::ExitStatus exitStatus) {
        VibeCutJob job;
        jobs->job(jobId, job);
        if (job.state == VibeCutJobState::CancelRequested) {
            jobs->markCancelled(jobId, QStringLiteral("Freeze detection cancelled."));
            process->deleteLater();
            return;
        }
        const QString stderrText = QString::fromUtf8(process->readAllStandardError());
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            jobs->markFailed(jobId, QStringLiteral("FFmpeg freezedetect failed (exit %1).").arg(exitCode));
            process->deleteLater();
            return;
        }

        const QList<QPair<double, double>> ranges = parseFreezeRanges(stderrText);
        QList<VibeCutMediaEvidenceRecord> records;
        int index = 0;
        for (const auto &range : ranges) {
            VibeCutMediaEvidenceRecord record;
            record.id = QStringLiteral("freeze:%1:%2:%3").arg(binId).arg(fingerprint.left(12)).arg(index++);
            record.sourceId = QStringLiteral("bin:%1").arg(binId);
            record.sourceFingerprint = fingerprint;
            record.extractorId = QString::fromLatin1(ExtractorId);
            record.extractorVersion = QString::fromLatin1(ExtractorVersion);
            record.kind = QStringLiteral("freeze_frame_range");
            record.startFrame = qMax(0, static_cast<int>(qRound64(range.first * fps)));
            record.endFrame = qMax(record.startFrame, static_cast<int>(qRound64(range.second * fps)));
            const double duration = qMax(0.0, range.second - range.first);
            record.text = QStringLiteral("frozen video freeze frame range %1 seconds").arg(duration, 0, 'f', 3);
            record.confidence = 1.0;
            record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            record.metadata = QJsonObject{{QStringLiteral("start_seconds"), range.first},
                                          {QStringLiteral("end_seconds"), range.second},
                                          {QStringLiteral("duration_seconds"), duration},
                                          {QStringLiteral("min_duration_ms"), minDurationMs},
                                          {QStringLiteral("noise_db"), noiseDb}};
            records.append(record);
        }

        QString error;
        if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(QStringLiteral("bin:%1").arg(binId), fingerprint,
                                                                  QString::fromLatin1(ExtractorId), QString::fromLatin1(ExtractorVersion),
                                                                  records, &error)) {
            jobs->markFailed(jobId, QStringLiteral("Freeze evidence persistence failed: %1").arg(error));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Detected %1 frozen-video range(s) and persisted evidence.").arg(records.size()));
        process->deleteLater();
    });

    QObject::connect(process, &QProcess::errorOccurred, tools, [jobs, process, jobId](QProcess::ProcessError processError) {
        if (processError == QProcess::Crashed) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && !job.terminal()) {
            jobs->markFailed(jobId, QStringLiteral("Could not start/run FFmpeg freezedetect: %1").arg(process->errorString()));
        }
    });

    process->start(ffmpeg, args);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job_id"), jobId},
                       {QStringLiteral("bin_id"), binId}, {QStringLiteral("source_fingerprint"), fingerprint},
                       {QStringLiteral("extractor_id"), QString::fromLatin1(ExtractorId)},
                       {QStringLiteral("extractor_version"), QString::fromLatin1(ExtractorVersion)},
                       {QStringLiteral("min_duration_ms"), minDurationMs}, {QStringLiteral("noise_db"), noiseDb},
                       {QStringLiteral("asynchronous"), true}};
}
} // namespace

bool registerVibeCutFreezeExtractorTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Freeze extractor requires the native VibeCutTools/JobManager surface.");
        return false;
    }
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("min_duration_ms"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 100}, {QStringLiteral("maximum"), 600000}}},
                                {QStringLiteral("noise_db"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), -100}, {QStringLiteral("maximum"), 0}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_freeze_refresh")},
                             {QStringLiteral("description"), QStringLiteral("Asynchronously run Kdenlive's configured FFmpeg freezedetect on one file-backed video bin asset, then persist frozen-video ranges as versioned extractor evidence. Returns a shared VibeCut job id and supports job_cancel.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_freeze_refresh");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    return surface.registerTool(schema, policy, [tools](const QJsonObject &input) { return startFreeze(tools, input); }, error);
}
