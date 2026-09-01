/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutshotextractortools.h"

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
#include <algorithm>

namespace {
constexpr auto ExtractorId = "shot_boundary";
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

QList<double> parseBoundaries(const QString &output)
{
    QList<double> seconds;
    const QRegularExpression expression(QStringLiteral("pts_time:([0-9]+(?:\\.[0-9]+)?)"));
    QRegularExpressionMatchIterator it = expression.globalMatch(output);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const double value = match.captured(1).toDouble(&ok);
        if (ok && value >= 0.0) seconds.append(value);
    }
    std::sort(seconds.begin(), seconds.end());
    seconds.erase(std::unique(seconds.begin(), seconds.end(), [](double a, double b) { return qAbs(a - b) < 0.0001; }), seconds.end());
    return seconds;
}

QJsonObject startShots(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    QString persistError;
    if (!VibeCutMediaEvidence::canPersistCurrent(&persistError)) return err(persistError);
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const double threshold = qBound(0.01, input.value(QStringLiteral("threshold")).toDouble(0.30), 1.0);
    const int minIntervalFrames = qBound(0, input.value(QStringLiteral("min_interval_frames")).toInt(0), 1000000);
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Shot detection requires a file-backed source."));
    if (!clip->hasVideo()) return err(QStringLiteral("Bin clip '%1' has no video stream.").arg(binId));
    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Source file is missing or invalid."));
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) return err(QStringLiteral("Kdenlive has no valid configured FFmpeg executable."));
    const QString fingerprint = statFingerprint(info);
    const double fps = pCore->getCurrentFps();
    if (fps <= 0.0) return err(QStringLiteral("Current project frame rate is invalid."));
    const int durationFrames = qMax(0, clip->getFramePlaytime());
    VibeCutJobManager *jobs = tools->jobManager();
    if (!jobs) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString jobId = jobs->createJob(QStringLiteral("media_shots"), QStringLiteral("Detect shots in %1").arg(info.fileName()), true);
    jobs->markRunning(jobId, QStringLiteral("FFmpeg scene detection is starting."));
    QProcess *process = new QProcess(tools);
    process->setProcessChannelMode(QProcess::MergedChannels);
    const QString filter = QStringLiteral("select='gt(scene,%1)',showinfo").arg(threshold, 0, 'f', 3);
    const QStringList args{QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("info"), QStringLiteral("-i"), info.absoluteFilePath(), QStringLiteral("-filter:v"), filter, QStringLiteral("-fps_mode"), QStringLiteral("vfr"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process, [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->kill();
    });
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), tools,
                     [jobs, process, jobId, binId, fingerprint, fps, threshold, minIntervalFrames, durationFrames](int exitCode, QProcess::ExitStatus exitStatus) {
        VibeCutJob job;
        jobs->job(jobId, job);
        if (job.state == VibeCutJobState::CancelRequested) {
            jobs->markCancelled(jobId, QStringLiteral("Shot detection cancelled."));
            process->deleteLater();
            return;
        }
        const QString output = QString::fromUtf8(process->readAll());
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            jobs->markFailed(jobId, QStringLiteral("FFmpeg scene detection failed (exit %1).").arg(exitCode));
            process->deleteLater();
            return;
        }
        const QList<double> raw = parseBoundaries(output);
        QList<int> boundaries;
        int previous = -1;
        for (double seconds : raw) {
            const int frame = qMax(0, static_cast<int>(qRound64(seconds * fps)));
            if (frame <= 0 || frame >= durationFrames) continue;
            if (previous >= 0 && minIntervalFrames > 0 && frame - previous < minIntervalFrames) continue;
            boundaries.append(frame);
            previous = frame;
        }
        QList<VibeCutMediaEvidenceRecord> records;
        int boundaryIndex = 0;
        for (int frame : boundaries) {
            VibeCutMediaEvidenceRecord boundary;
            boundary.id = QStringLiteral("shot_boundary:%1:%2:%3").arg(binId).arg(fingerprint.left(12)).arg(boundaryIndex++);
            boundary.sourceId = QStringLiteral("bin:%1").arg(binId);
            boundary.sourceFingerprint = fingerprint;
            boundary.extractorId = QString::fromLatin1(ExtractorId);
            boundary.extractorVersion = QString::fromLatin1(ExtractorVersion);
            boundary.kind = QStringLiteral("shot_boundary");
            boundary.startFrame = frame;
            boundary.endFrame = frame;
            boundary.text = QStringLiteral("shot boundary scene change at frame %1").arg(frame);
            boundary.confidence = -1.0;
            boundary.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            boundary.metadata = QJsonObject{{QStringLiteral("threshold"), threshold}, {QStringLiteral("min_interval_frames"), minIntervalFrames}};
            records.append(boundary);
        }
        QList<int> cuts;
        cuts.append(0);
        for (int frame : boundaries) cuts.append(frame);
        cuts.append(durationFrames);
        for (int i = 0; i + 1 < cuts.size(); ++i) {
            const int start = cuts.at(i);
            const int end = cuts.at(i + 1);
            if (end <= start) continue;
            VibeCutMediaEvidenceRecord segment;
            segment.id = QStringLiteral("shot_segment:%1:%2:%3").arg(binId).arg(fingerprint.left(12)).arg(i);
            segment.sourceId = QStringLiteral("bin:%1").arg(binId);
            segment.sourceFingerprint = fingerprint;
            segment.extractorId = QString::fromLatin1(ExtractorId);
            segment.extractorVersion = QString::fromLatin1(ExtractorVersion);
            segment.kind = QStringLiteral("shot_segment");
            segment.startFrame = start;
            segment.endFrame = end;
            segment.text = QStringLiteral("shot segment frames %1 to %2").arg(start).arg(end);
            segment.confidence = -1.0;
            segment.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            segment.metadata = QJsonObject{{QStringLiteral("segment_index"), i}, {QStringLiteral("threshold"), threshold}};
            records.append(segment);
        }
        QString error;
        if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(QStringLiteral("bin:%1").arg(binId), fingerprint, QString::fromLatin1(ExtractorId), QString::fromLatin1(ExtractorVersion), records, &error)) {
            jobs->markFailed(jobId, QStringLiteral("Shot evidence persistence failed: %1").arg(error));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Detected %1 shot boundary(ies) and %2 shot segment(s).").arg(boundaries.size()).arg(qMax(0, cuts.size() - 1)));
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, tools, [jobs, process, jobId](QProcess::ProcessError processError) {
        if (processError == QProcess::Crashed) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && !job.terminal()) jobs->markFailed(jobId, QStringLiteral("Could not start/run FFmpeg scene detection: %1").arg(process->errorString()));
    });
    process->start(ffmpeg, args);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job_id"), jobId}, {QStringLiteral("bin_id"), binId}, {QStringLiteral("source_fingerprint"), fingerprint}, {QStringLiteral("extractor_id"), QString::fromLatin1(ExtractorId)}, {QStringLiteral("extractor_version"), QString::fromLatin1(ExtractorVersion)}, {QStringLiteral("threshold"), threshold}, {QStringLiteral("min_interval_frames"), minIntervalFrames}, {QStringLiteral("asynchronous"), true}};
}
} // namespace

bool registerVibeCutShotExtractorTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Shot extractor requires the native VibeCutTools/JobManager surface.");
        return false;
    }
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}, {QStringLiteral("threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.01}, {QStringLiteral("maximum"), 1.0}}}, {QStringLiteral("min_interval_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}}}, {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}}, {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_shots_refresh")}, {QStringLiteral("description"), QStringLiteral("Asynchronously run the same FFmpeg scene-change detector used by Kdenlive on one file-backed video bin asset, then persist non-destructive shot-boundary and shot-segment frame evidence. Returns a shared VibeCut job id and supports job_cancel.")}, {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_shots_refresh");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    return surface.registerTool(schema, policy, [tools](const QJsonObject &input) { return startShots(tools, input); }, error);
}
