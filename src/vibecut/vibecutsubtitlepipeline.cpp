/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecuttools.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"
#include "vibecutpreflighttools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::shared_ptr<TimelineItemModel> currentModel()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->model() : nullptr;
}

QString reserveTemporaryPath(const QString &pattern, QString &error)
{
    QTemporaryFile file(QDir::temp().absoluteFilePath(pattern));
    file.setAutoRemove(false);
    if (!file.open()) {
        error = QStringLiteral("Could not reserve temporary file '%1': %2").arg(pattern, file.errorString());
        return QString();
    }
    const QString path = file.fileName();
    file.close();
    return path;
}

QString preflightBlockerSummary(const QJsonObject &preflight)
{
    QStringList messages;
    for (const QJsonValue &value : preflight.value(QStringLiteral("blockers")).toArray()) {
        const QString message = value.toObject().value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) messages.append(message);
    }
    return messages.join(QStringLiteral("; "));
}

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

double srtTimeSeconds(const QString &value, bool *ok)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{2}):(\\d{2}):(\\d{2})[,.](\\d{3})$"));
    const QRegularExpressionMatch match = re.match(value.trimmed());
    if (!match.hasMatch()) {
        if (ok) *ok = false;
        return 0.0;
    }
    if (ok) *ok = true;
    return match.captured(1).toInt() * 3600.0 + match.captured(2).toInt() * 60.0 + match.captured(3).toInt() + match.captured(4).toInt() / 1000.0;
}

QList<VibeCutMediaEvidenceRecord> transcriptEvidence(const QString &srtPath, const QString &sourceId, const QString &sourceFingerprint,
                                                     const QString &modelName, int zoneIn, double fps)
{
    QList<VibeCutMediaEvidenceRecord> records;
    QFile file(srtPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || fps <= 0.0) return records;
    const QString content = QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList blocks = content.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts);
    const QRegularExpression rangeRe(QStringLiteral("(\\d{2}:\\d{2}:\\d{2}[,.]\\d{3})\\s*-->\\s*(\\d{2}:\\d{2}:\\d{2}[,.]\\d{3})"));
    int index = 0;
    for (const QString &block : blocks) {
        const QStringList lines = block.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        int rangeLine = -1;
        QRegularExpressionMatch rangeMatch;
        for (int i = 0; i < lines.size(); ++i) {
            rangeMatch = rangeRe.match(lines.at(i));
            if (rangeMatch.hasMatch()) { rangeLine = i; break; }
        }
        if (rangeLine < 0) continue;
        bool startOk = false, endOk = false;
        const double startSeconds = srtTimeSeconds(rangeMatch.captured(1), &startOk);
        const double endSeconds = srtTimeSeconds(rangeMatch.captured(2), &endOk);
        if (!startOk || !endOk || endSeconds < startSeconds) continue;
        QStringList textLines;
        for (int i = rangeLine + 1; i < lines.size(); ++i) textLines.append(lines.at(i).trimmed());
        const QString text = textLines.join(QLatin1Char(' ')).trimmed();
        if (text.isEmpty()) continue;

        VibeCutMediaEvidenceRecord record;
        record.id = QStringLiteral("whisper:%1:%2").arg(sourceFingerprint.left(16)).arg(index++);
        record.sourceId = sourceId;
        record.sourceFingerprint = sourceFingerprint;
        record.extractorId = QStringLiteral("whisper_transcript");
        record.extractorVersion = QStringLiteral("model:%1").arg(modelName);
        record.kind = QStringLiteral("transcript_segment");
        record.startFrame = zoneIn + qMax(0, static_cast<int>(qRound64(startSeconds * fps)));
        record.endFrame = zoneIn + qMax(0, static_cast<int>(qRound64(endSeconds * fps)));
        record.text = text;
        record.confidence = -1.0;
        record.metadata = QJsonObject{{QStringLiteral("model"), modelName},
                                      {QStringLiteral("start_seconds"), startSeconds},
                                      {QStringLiteral("end_seconds"), endSeconds},
                                      {QStringLiteral("source_kind"), QStringLiteral("timeline_audio_snapshot")}};
        records.append(record);
    }
    return records;
}
} // namespace

QJsonObject VibeCutTools::startAsyncSubtitleGeneration(const QJsonObject &input)
{
    const QJsonObject preflight = vibeCutProjectPreflight();
    if (!preflight.value(QStringLiteral("ok")).toBool(false)) return err(QStringLiteral("Project preflight failed: %1").arg(preflight.value(QStringLiteral("error")).toString()));
    if (!preflight.value(QStringLiteral("ready_for_long_jobs")).toBool(false)) {
        const QString blockers = preflightBlockerSummary(preflight);
        return err(QStringLiteral("Subtitle generation blocked by project preflight%1").arg(blockers.isEmpty() ? QStringLiteral(".") : QStringLiteral(": %1").arg(blockers)));
    }

    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    if (m_subtitleJobRunning) return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), false}, {QStringLiteral("note"), QStringLiteral("A subtitle generation job is already running.")}};
    if (!vibecutDepsReady()) return err(QStringLiteral("Whisper is not set up yet. Call speech_setup first."));

    const QMap<QString, QString> urls = whisperModelUrls();
    QStringList installed;
    for (auto it = urls.constBegin(); it != urls.constEnd(); ++it) if (whisperModelDownloaded(it.key(), urls)) installed.append(it.key());
    if (installed.isEmpty()) return err(QStringLiteral("No Whisper model is installed yet. Call speech_setup first."));
    QString useModel = input.value(QStringLiteral("model")).toString();
    if (useModel.isEmpty() || !installed.contains(useModel)) useModel = installed.contains(QStringLiteral("turbo")) ? QStringLiteral("turbo") : installed.first();

    int zoneIn = 0;
    int zoneOut = model->duration();
    if (input.contains(QStringLiteral("clip_id"))) {
        const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
        if (!model->isClip(clipId)) return err(QStringLiteral("Clip id %1 does not exist on the timeline.").arg(clipId));
        zoneIn = model->getClipPosition(clipId);
        zoneOut = zoneIn + model->getClipPlaytime(clipId);
    }
    if (zoneOut <= zoneIn) return err(QStringLiteral("Nothing to transcribe (empty zone)."));
    if (!ensureSubtitleTrack(model)) return err(QStringLiteral("Could not create a subtitle track."));

    QString tempError;
    const QString sceneList = reserveTemporaryPath(QStringLiteral("vibecut-XXXXXX.mlt"), tempError);
    if (sceneList.isEmpty()) return err(tempError);
    const QString audioPath = reserveTemporaryPath(QStringLiteral("vibecut-XXXXXX.wav"), tempError);
    if (audioPath.isEmpty()) { QFile::remove(sceneList); return err(tempError); }
    QFile::remove(audioPath);
    model->sceneList(QDir::temp().absolutePath(), sceneList);
    if (!QFile::exists(sceneList) || QFileInfo(sceneList).size() == 0) { QFile::remove(sceneList); return err(QStringLiteral("Could not snapshot the timeline for background audio export.")); }
    const QString snapshotFingerprint = hashFile(sceneList);
    const QString transcriptSourceId = QStringLiteral("timeline:%1:zone:%2-%3").arg(model->uuid().toString(QUuid::WithoutBraces)).arg(zoneIn).arg(zoneOut);

    QString meltPath = KdenliveSettings::meltpath();
    if (meltPath.isEmpty() || !QFileInfo::exists(meltPath)) meltPath = QStandardPaths::findExecutable(QStringLiteral("melt"));
    if (meltPath.isEmpty()) { QFile::remove(sceneList); return err(QStringLiteral("Could not find the MLT melt renderer for background audio export.")); }

    const quint64 baseRevision = projectRevision();
    VibeCutJobManager *jobs = jobManager();
    const QString jobId = jobs->createJob(QStringLiteral("subtitle_generation"), QStringLiteral("Generate subtitles"), false);
    jobs->markRunning(jobId, QStringLiteral("Exporting timeline audio"));
    jobs->setProgress(jobId, 2, QStringLiteral("Exporting timeline audio"));
    m_subtitleJobRunning = true;
    Q_EMIT backgroundProgress(QStringLiteral("Exporting audio in the background for Whisper…"));

    const auto startWhisper = [this, jobs, jobId, useModel, audioPath, zoneIn, baseRevision, snapshotFingerprint, transcriptSourceId]() {
        const QString script = whisperScript(QStringLiteral("whisper/whispertosrt.py"));
        if (script.isEmpty()) {
            m_subtitleJobRunning = false; QFile::remove(audioPath); jobs->markFailed(jobId, QStringLiteral("Could not find Kdenlive's bundled Whisper transcription script."));
            Q_EMIT backgroundProgress(QStringLiteral("Subtitle generation failed: Whisper transcription script not found.")); return;
        }
        const QString srtPath = QDir::temp().absoluteFilePath(QFileInfo(audioPath).completeBaseName() + QStringLiteral(".srt"));
        QFile::remove(srtPath);
        const bool cuda = vibecutCudaAvailable();
        QStringList arguments = {script, audioPath, useModel, QStringLiteral("ffmpeg_path=%1").arg(KdenliveSettings::ffmpegpath()), QStringLiteral("device=%1").arg(cuda ? QStringLiteral("cuda") : QStringLiteral("cpu"))};
        if (KdenliveSettings::whisperDisableFP16()) arguments.append(QStringLiteral("fp16=False"));
        jobs->setProgress(jobId, 20, QStringLiteral("Transcribing with Whisper"));
        Q_EMIT backgroundProgress(QStringLiteral("Transcribing with Whisper (%1, %2)…").arg(useModel, cuda ? QStringLiteral("GPU") : QStringLiteral("CPU - no CUDA device found")));
        auto *proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        const auto fail = [this, jobs, jobId, proc, audioPath, srtPath](const QString &message) {
            m_subtitleJobRunning = false; QFile::remove(audioPath); QFile::remove(srtPath); jobs->markFailed(jobId, message);
            Q_EMIT backgroundProgress(QStringLiteral("Subtitle generation failed: %1").arg(message)); proc->deleteLater();
        };
        connect(proc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
                [this, jobs, jobId, proc, audioPath, srtPath, zoneIn, baseRevision, fail, snapshotFingerprint, transcriptSourceId, useModel](int exitCode, QProcess::ExitStatus status) {
                    if (status == QProcess::CrashExit || exitCode != 0) {
                        const QString output = QString::fromUtf8(proc->readAll()).right(2000).trimmed();
                        fail(output.isEmpty() ? QStringLiteral("Whisper exited with code %1").arg(exitCode) : output); return;
                    }
                    QFile::remove(audioPath);
                    if (!QFile::exists(srtPath)) { fail(QStringLiteral("Whisper produced no subtitle file.")); return; }
                    jobs->setProgress(jobId, 90, QStringLiteral("Verifying live project state"));
                    if (projectRevision() != baseRevision) {
                        QFile::remove(srtPath); m_subtitleJobRunning = false;
                        const QString message = QStringLiteral("The project changed while transcription was running; stale subtitles were not imported.");
                        jobs->markFailed(jobId, message); Q_EMIT backgroundProgress(QStringLiteral("Subtitle import stopped safely: %1").arg(message)); proc->deleteLater(); return;
                    }
                    const std::shared_ptr<TimelineItemModel> liveModel = currentModel();
                    const std::shared_ptr<SubtitleModel> subtitleModel = liveModel ? liveModel->getSubtitleModel() : nullptr;
                    if (!subtitleModel) { fail(QStringLiteral("Transcription finished, but the active project no longer has the subtitle model.")); return; }
                    if (QFileInfo(srtPath).size() == 0) {
                        QFile::remove(srtPath); m_subtitleJobRunning = false; jobs->markSucceeded(jobId, QStringLiteral("Transcription completed; no speech segments were produced."));
                        Q_EMIT backgroundProgress(QStringLiteral("✓ Transcription completed; no speech segments were detected.")); proc->deleteLater(); return;
                    }

                    QString evidenceWarning;
                    if (!snapshotFingerprint.isEmpty()) {
                        QString persistenceError;
                        if (VibeCutMediaEvidence::canPersistCurrent(&persistenceError)) {
                            const QList<VibeCutMediaEvidenceRecord> records = transcriptEvidence(srtPath, transcriptSourceId, snapshotFingerprint, useModel, zoneIn, pCore->getCurrentFps());
                            if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(transcriptSourceId, snapshotFingerprint, QStringLiteral("whisper_transcript"), QStringLiteral("model:%1").arg(useModel), records, &persistenceError)) {
                                evidenceWarning = QStringLiteral(" Transcript evidence was not persisted: %1").arg(persistenceError);
                            }
                        } else {
                            evidenceWarning = QStringLiteral(" Transcript evidence was not persisted: %1").arg(persistenceError);
                        }
                    }

                    const int beforeCount = subtitleModel->count();
                    subtitleModel->importSubtitle(srtPath, zoneIn, true);
                    const int afterCount = subtitleModel->count();
                    QFile::remove(srtPath); m_subtitleJobRunning = false;
                    if (afterCount <= beforeCount) {
                        const QString message = QStringLiteral("Subtitle import completed without adding any subtitle entries; verification failed.");
                        jobs->markFailed(jobId, message); Q_EMIT backgroundProgress(QStringLiteral("Subtitle generation failed verification: %1").arg(message)); proc->deleteLater(); return;
                    }
                    const int added = afterCount - beforeCount;
                    jobs->markSucceeded(jobId, QStringLiteral("Imported %1 verified subtitle entries.%2").arg(added).arg(evidenceWarning));
                    Q_EMIT backgroundProgress(QStringLiteral("✓ Imported and verified %1 subtitle entries.%2").arg(added).arg(evidenceWarning)); proc->deleteLater();
                });
        connect(proc, &QProcess::errorOccurred, this, [fail](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) fail(QStringLiteral("Could not launch the Whisper environment's Python interpreter; call speech_setup again.")); });
        proc->start(vibecutVenvPython(), arguments);
    };

    auto *exportProc = new QProcess(this);
    exportProc->setProcessChannelMode(QProcess::MergedChannels);
    const auto failExport = [this, jobs, jobId, exportProc, sceneList, audioPath](const QString &message) {
        m_subtitleJobRunning = false; QFile::remove(sceneList); QFile::remove(audioPath); jobs->markFailed(jobId, message);
        Q_EMIT backgroundProgress(QStringLiteral("Subtitle generation failed during audio export: %1").arg(message)); exportProc->deleteLater();
    };
    connect(exportProc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [jobs, jobId, exportProc, sceneList, audioPath, startWhisper, failExport](int exitCode, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(exportProc->readAll()).right(2000).trimmed(); QFile::remove(sceneList);
                if (status == QProcess::CrashExit || exitCode != 0) { failExport(output.isEmpty() ? QStringLiteral("melt exited with code %1").arg(exitCode) : output); return; }
                if (!QFile::exists(audioPath) || QFileInfo(audioPath).size() == 0) { failExport(QStringLiteral("Background audio export produced no WAV output.")); return; }
                jobs->setProgress(jobId, 15, QStringLiteral("Audio export complete")); exportProc->deleteLater(); startWhisper();
            });
    connect(exportProc, &QProcess::errorOccurred, this, [failExport](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) failExport(QStringLiteral("Could not launch the MLT melt renderer.")); });
    const QStringList exportArguments = {sceneList, QStringLiteral("in=%1").arg(zoneIn), QStringLiteral("out=%1").arg(zoneOut), QStringLiteral("-consumer"), QStringLiteral("avformat:%1").arg(audioPath), QStringLiteral("properties=WAV"), QStringLiteral("terminate_on_pause=1"), QStringLiteral("real_time=-1")};
    exportProc->start(meltPath, exportArguments);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true}, {QStringLiteral("job_id"), jobId}, {QStringLiteral("stage"), QStringLiteral("audio_export")}, {QStringLiteral("model"), useModel}, {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)}, {QStringLiteral("transcript_source_id"), transcriptSourceId}, {QStringLiteral("snapshot_fingerprint"), snapshotFingerprint}, {QStringLiteral("note"), QStringLiteral("Audio export and Whisper transcription are running asynchronously. Subtitle import is rejected if the project changes; when possible, timestamped Whisper evidence is persisted separately from subtitle-track state.")}};
}
