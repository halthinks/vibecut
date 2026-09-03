/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutr128extractortools.h"

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
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QtMath>

#include <cmath>

namespace {
constexpr auto ExtractorId = "audio_r128";
constexpr auto ExtractorVersion = "1.0.0";

struct R128Sample {
    double ptsSeconds = -1.0;
    double momentaryLufs = -120.691;
    double shortTermLufs = -120.691;
    double integratedLufs = -70.0;
    double loudnessRangeLu = 0.0;
    double truePeakLinear = -1.0;
    bool hasMomentary = false;
    bool hasShortTerm = false;
    bool hasIntegrated = false;
    bool hasLra = false;
    bool hasTruePeak = false;
};

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

bool parseDoubleAfterEquals(const QString &line, double &value)
{
    const int equals = line.indexOf(QLatin1Char('='));
    if (equals < 0) return false;
    bool ok = false;
    const double parsed = line.mid(equals + 1).trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

QList<R128Sample> parseR128Metadata(const QString &output)
{
    QList<R128Sample> result;
    R128Sample current;
    bool active = false;
    const QRegularExpression ptsExpression(QStringLiteral("pts_time:([-+]?[0-9]+(?:\\.[0-9]+)?)"));
    auto flush = [&]() {
        if (active && current.ptsSeconds >= 0.0 && current.hasMomentary) result.append(current);
        current = R128Sample();
        active = false;
    };

    for (const QString &rawLine : output.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("frame:"))) {
            flush();
            const QRegularExpressionMatch match = ptsExpression.match(line);
            if (match.hasMatch()) {
                bool ok = false;
                current.ptsSeconds = match.captured(1).toDouble(&ok);
                active = ok && current.ptsSeconds >= 0.0;
            }
            continue;
        }
        if (!active) continue;
        double value = 0.0;
        if (line.startsWith(QStringLiteral("lavfi.r128.M=")) && parseDoubleAfterEquals(line, value)) {
            current.momentaryLufs = value;
            current.hasMomentary = true;
        } else if (line.startsWith(QStringLiteral("lavfi.r128.S=")) && parseDoubleAfterEquals(line, value)) {
            current.shortTermLufs = value;
            current.hasShortTerm = true;
        } else if (line.startsWith(QStringLiteral("lavfi.r128.I=")) && parseDoubleAfterEquals(line, value)) {
            current.integratedLufs = value;
            current.hasIntegrated = true;
        } else if (line.startsWith(QStringLiteral("lavfi.r128.LRA=")) && parseDoubleAfterEquals(line, value)) {
            current.loudnessRangeLu = value;
            current.hasLra = true;
        } else if (line.startsWith(QStringLiteral("lavfi.r128.true_peak=")) && parseDoubleAfterEquals(line, value)) {
            current.truePeakLinear = value;
            current.hasTruePeak = value >= 0.0;
        }
    }
    flush();
    return result;
}

QJsonObject startR128(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    QString persistError;
    if (!VibeCutMediaEvidence::canPersistCurrent(&persistError)) return err(persistError);

    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Audio profiling requires a file-backed source."));
    if (!clip->hasAudio()) return err(QStringLiteral("Bin clip '%1' has no audio stream.").arg(binId));

    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Source file is missing or invalid."));
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) return err(QStringLiteral("Kdenlive has no valid configured FFmpeg executable."));
    const double fps = pCore->getCurrentFps();
    if (fps <= 0.0) return err(QStringLiteral("Current project frame rate is invalid."));
    const int durationFrames = qMax(0, clip->getFramePlaytime());
    const int startFrame = input.contains(QStringLiteral("start_frame")) ? input.value(QStringLiteral("start_frame")).toInt(-1) : 0;
    const int endFrame = input.contains(QStringLiteral("end_frame")) ? input.value(QStringLiteral("end_frame")).toInt(-1) : durationFrames;
    if (startFrame < 0 || endFrame <= startFrame || endFrame > durationFrames) {
        return err(QStringLiteral("Audio profile frame bounds must satisfy 0 <= start_frame < end_frame <= %1.").arg(durationFrames));
    }

    // ebur128 metadata is emitted at 10 Hz. Quantize requested evidence
    // sampling to integer 100 ms steps so the downsampling selector is exact.
    const int requestedIntervalMs = qBound(100, input.value(QStringLiteral("sample_interval_ms")).toInt(500), 10000);
    const int metadataStride = qBound(1, qRound(requestedIntervalMs / 100.0), 100);
    const int sampleIntervalMs = metadataStride * 100;
    const int maxSamples = qBound(1, input.value(QStringLiteral("max_samples")).toInt(10000), 50000);
    const double startSeconds = static_cast<double>(startFrame) / fps;
    const double endSeconds = static_cast<double>(endFrame) / fps;

    VibeCutJobManager *jobs = tools->jobManager();
    if (!jobs) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString fingerprint = statFingerprint(info);
    const QString jobId = jobs->createJob(QStringLiteral("audio_r128"),
                                          QStringLiteral("Measure EBU R128 audio profile · %1").arg(info.fileName()), true);
    jobs->markRunning(jobId, QStringLiteral("FFmpeg EBU R128 measurement is starting."));

    auto *process = new QProcess(tools);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const QString filter = QStringLiteral("atrim=start=%1:end=%2,asetpts=PTS-STARTPTS,ebur128=metadata=1:peak=true,aselect='not(mod(n\\,%3))',ametadata=print:file='pipe\\:1'")
                               .arg(startSeconds, 0, 'f', 9)
                               .arg(endSeconds, 0, 'f', 9)
                               .arg(metadataStride);
    const QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), info.absoluteFilePath(), QStringLiteral("-vn"),
                           QStringLiteral("-af"), filter, QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};

    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process, [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->terminate();
    });

    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), tools,
                     [jobs, process, jobId, binId, fingerprint, fps, startFrame, endFrame, sampleIntervalMs, maxSamples]
                     (int exitCode, QProcess::ExitStatus exitStatus) {
        VibeCutJob job;
        jobs->job(jobId, job);
        if (job.state == VibeCutJobState::CancelRequested) {
            jobs->markCancelled(jobId, QStringLiteral("Audio profile measurement cancelled."));
            process->deleteLater();
            return;
        }
        const QByteArray stdoutData = process->readAllStandardOutput();
        if (stdoutData.size() > 64 * 1024 * 1024) {
            jobs->markFailed(jobId, QStringLiteral("EBU R128 metadata exceeded the 64 MiB safety limit."));
            process->deleteLater();
            return;
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString stderrText = QString::fromUtf8(process->readAllStandardError()).right(4000).trimmed();
            jobs->markFailed(jobId, stderrText.isEmpty() ? QStringLiteral("FFmpeg EBU R128 measurement failed (exit %1).").arg(exitCode) : stderrText);
            process->deleteLater();
            return;
        }

        const QList<R128Sample> samples = parseR128Metadata(QString::fromUtf8(stdoutData));
        if (samples.size() > maxSamples) {
            jobs->markFailed(jobId, QStringLiteral("EBU R128 produced %1 samples, exceeding requested max_samples=%2.").arg(samples.size()).arg(maxSamples));
            process->deleteLater();
            return;
        }

        QList<VibeCutMediaEvidenceRecord> records;
        int index = 0;
        const int emissionFrames = qMax(1, static_cast<int>(qCeil(fps * 0.1)));
        for (const R128Sample &sample : samples) {
            // The first ~300 ms do not contain a complete 400 ms momentary
            // loudness window. Keep the measurement truthful by omitting that
            // warm-up output instead of treating the EBU floor sentinel as an
            // observed quiet signal.
            if (sample.ptsSeconds < 0.299) continue;
            const int absoluteFrame = qBound(startFrame,
                                             startFrame + static_cast<int>(qRound64(sample.ptsSeconds * fps)),
                                             endFrame - 1);
            const int recordEnd = qMin(endFrame, absoluteFrame + emissionFrames);
            if (recordEnd <= absoluteFrame) continue;

            VibeCutMediaEvidenceRecord record;
            record.id = QStringLiteral("r128:%1:%2:%3").arg(binId).arg(fingerprint.left(12)).arg(index++);
            record.sourceId = QStringLiteral("bin:%1").arg(binId);
            record.sourceFingerprint = fingerprint;
            record.extractorId = QString::fromLatin1(ExtractorId);
            record.extractorVersion = QString::fromLatin1(ExtractorVersion);
            record.kind = QStringLiteral("audio_loudness_sample");
            record.startFrame = absoluteFrame;
            record.endFrame = recordEnd;
            record.text = QStringLiteral("momentary loudness %1 LUFS at source frame %2")
                              .arg(sample.momentaryLufs, 0, 'f', 2).arg(absoluteFrame);
            record.confidence = 1.0;
            record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            QJsonObject metadata{{QStringLiteral("sample_pts_seconds"), sample.ptsSeconds},
                                 {QStringLiteral("sample_interval_ms"), sampleIntervalMs},
                                 {QStringLiteral("momentary_lufs"), sample.momentaryLufs},
                                 {QStringLiteral("momentary_window_ms"), 400},
                                 {QStringLiteral("short_term_window_ms"), 3000},
                                 {QStringLiteral("integrated_lufs"), sample.integratedLufs},
                                 {QStringLiteral("loudness_range_lu"), sample.loudnessRangeLu},
                                 {QStringLiteral("short_term_warmup"), sample.ptsSeconds < 2.9}};
            if (sample.hasShortTerm) metadata.insert(QStringLiteral("short_term_lufs"), sample.shortTermLufs);
            if (sample.hasTruePeak) {
                metadata.insert(QStringLiteral("true_peak_linear"), sample.truePeakLinear);
                if (sample.truePeakLinear > 0.0) {
                    metadata.insert(QStringLiteral("true_peak_dbfs"), 20.0 * std::log10(sample.truePeakLinear));
                }
            }
            record.metadata = metadata;
            records.append(record);
        }

        QString error;
        if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(QStringLiteral("bin:%1").arg(binId), fingerprint,
                                                                  QString::fromLatin1(ExtractorId), QString::fromLatin1(ExtractorVersion), records, &error)) {
            jobs->markFailed(jobId, QStringLiteral("EBU R128 evidence persistence failed: %1").arg(error));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, QStringLiteral("Persisted %1 bounded EBU R128 loudness observation(s).").arg(records.size()));
        process->deleteLater();
    });

    QObject::connect(process, &QProcess::errorOccurred, tools, [jobs, process, jobId](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart) return;
        jobs->markFailed(jobId, QStringLiteral("Could not launch Kdenlive's configured FFmpeg for EBU R128 measurement."));
        process->deleteLater();
    });

    process->start(ffmpeg, args);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job_id"), jobId},
                       {QStringLiteral("bin_id"), binId}, {QStringLiteral("source_fingerprint"), fingerprint},
                       {QStringLiteral("extractor_id"), QString::fromLatin1(ExtractorId)},
                       {QStringLiteral("extractor_version"), QString::fromLatin1(ExtractorVersion)},
                       {QStringLiteral("start_frame"), startFrame}, {QStringLiteral("end_frame"), endFrame},
                       {QStringLiteral("sample_interval_ms"), sampleIntervalMs}, {QStringLiteral("max_samples"), maxSamples},
                       {QStringLiteral("asynchronous"), true}};
}
} // namespace

bool registerVibeCutR128ExtractorTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("EBU R128 extractor requires the native VibeCutTools/JobManager surface.");
        return false;
    }
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                {QStringLiteral("sample_interval_ms"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 100}, {QStringLiteral("maximum"), 10000}}},
                                {QStringLiteral("max_samples"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 50000}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_audio_profile_refresh");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Measure bounded EBU R128 audio loudness observations for one file-backed source using Kdenlive's configured FFmpeg. Persists timestamped momentary/short-term/integrated loudness and true-peak provenance as measurement evidence; it does not label speech, music, noise or room tone. Sampling is bounded and cancellable.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [tools](const QJsonObject &request) { return startR128(tools, request); }, error);
}
