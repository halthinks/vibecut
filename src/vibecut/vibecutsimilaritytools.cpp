/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsimilaritytools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "vibecutbroll.h"
#include "vibecutcontinuity.h"
#include "vibecutcrossmodaltools.h"
#include "vibecutduplicatecandidates.h"
#include "vibecutduplicatefusion.h"
#include "vibecuteditorialeval.h"
#include "vibecuthighlights.h"
#include "vibecuthybridsearch.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"
#include "vibecutnarrative.h"
#include "vibecutpacing.h"
#include "vibecutroughcutalternatives.h"
#include "vibecutroughcutrelevance.h"
#include "vibecutroughcutsynthesis.h"
#include "vibecutsemantictools.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace {
constexpr auto ExtractorId = "mpeg7_similarity";
constexpr auto ExtractorVersion = "1.0.0";

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' + QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QString pairFingerprint(const QString &a, const QString &b)
{
    const QByteArray payload = a.toUtf8() + '\n' + b.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QJsonObject startSimilarity(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    QString persistError;
    if (!VibeCutMediaEvidence::canPersistCurrent(&persistError)) return err(persistError);

    const QString firstId = input.value(QStringLiteral("first_bin_id")).toString().trimmed();
    const QString secondId = input.value(QStringLiteral("second_bin_id")).toString().trimmed();
    if (firstId.isEmpty() || secondId.isEmpty()) return err(QStringLiteral("first_bin_id and second_bin_id must not be empty."));
    if (firstId == secondId) return err(QStringLiteral("Similarity comparison requires two distinct bin assets."));

    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> first = model ? model->getClipByBinID(firstId) : nullptr;
    const std::shared_ptr<ProjectClip> second = model ? model->getClipByBinID(secondId) : nullptr;
    if (!first || !second) return err(QStringLiteral("Both bin assets must exist."));
    if (!first->hasUrl() || !second->hasUrl() || !first->hasVideo() || !second->hasVideo()) {
        return err(QStringLiteral("Similarity comparison requires two file-backed video assets."));
    }

    const QFileInfo firstInfo(first->url());
    const QFileInfo secondInfo(second->url());
    if (!firstInfo.exists() || !firstInfo.isFile() || !secondInfo.exists() || !secondInfo.isFile()) {
        return err(QStringLiteral("Both source files must exist."));
    }
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) return err(QStringLiteral("Kdenlive has no valid configured FFmpeg executable."));

    const QString firstFingerprint = statFingerprint(firstInfo);
    const QString secondFingerprint = statFingerprint(secondInfo);
    const QString combinedFingerprint = pairFingerprint(firstFingerprint, secondFingerprint);
    const QString pairSourceId = QStringLiteral("pair:bin:%1:bin:%2").arg(firstId, secondId);

    VibeCutJobManager *jobs = tools->jobManager();
    if (!jobs) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString jobId = jobs->createJob(QStringLiteral("media_similarity"),
                                          QStringLiteral("Compare %1 and %2").arg(firstInfo.fileName(), secondInfo.fileName()), true);
    jobs->markRunning(jobId, QStringLiteral("FFmpeg MPEG-7 signature comparison is starting."));

    QProcess *process = new QProcess(tools);
    process->setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("info"),
                           QStringLiteral("-i"), firstInfo.absoluteFilePath(), QStringLiteral("-i"), secondInfo.absoluteFilePath(),
                           QStringLiteral("-filter_complex"), QStringLiteral("[0:v][1:v]signature=nb_inputs=2:detectmode=full"),
                           QStringLiteral("-map"), QStringLiteral("0:v"), QStringLiteral("-map"), QStringLiteral("1:v"),
                           QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};

    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process, [jobs, process, jobId](const QString &changedId) {
        if (changedId != jobId || process->state() == QProcess::NotRunning) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && job.state == VibeCutJobState::CancelRequested) process->kill();
    });

    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), tools,
                     [jobs, process, jobId, firstId, secondId, firstFingerprint, secondFingerprint, combinedFingerprint, pairSourceId]
                     (int exitCode, QProcess::ExitStatus exitStatus) {
        VibeCutJob job;
        jobs->job(jobId, job);
        if (job.state == VibeCutJobState::CancelRequested) {
            jobs->markCancelled(jobId, QStringLiteral("Media similarity comparison cancelled."));
            process->deleteLater();
            return;
        }
        const QString output = QString::fromUtf8(process->readAll());
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            jobs->markFailed(jobId, QStringLiteral("FFmpeg signature comparison failed (exit %1).").arg(exitCode));
            process->deleteLater();
            return;
        }

        const QRegularExpression re(QStringLiteral("matching of video\\s+([0-9]+)\\s+at\\s+([0-9]+(?:\\.[0-9]+)?)\\s+and\\s+([0-9]+)\\s+at\\s+([0-9]+(?:\\.[0-9]+)?),\\s+([0-9]+)\\s+frames matching"));
        const QRegularExpressionMatch match = re.match(output);
        const bool matched = match.hasMatch();
        const int matchingFrames = matched ? match.captured(5).toInt() : 0;
        const double firstSeconds = matched ? match.captured(2).toDouble() : -1.0;
        const double secondSeconds = matched ? match.captured(4).toDouble() : -1.0;

        VibeCutMediaEvidenceRecord record;
        record.id = QStringLiteral("similarity:%1:%2:%3").arg(firstId, secondId, combinedFingerprint.left(12));
        record.sourceId = pairSourceId;
        record.sourceFingerprint = combinedFingerprint;
        record.extractorId = QString::fromLatin1(ExtractorId);
        record.extractorVersion = QString::fromLatin1(ExtractorVersion);
        record.kind = matched ? QStringLiteral("video_similarity_match") : QStringLiteral("video_similarity_no_match");
        record.text = matched ? QStringLiteral("video similarity near duplicate match %1 frames").arg(matchingFrames)
                              : QStringLiteral("video similarity no MPEG-7 match");
        record.confidence = 1.0;
        record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        record.metadata = QJsonObject{{QStringLiteral("first_bin_id"), firstId}, {QStringLiteral("second_bin_id"), secondId},
                                      {QStringLiteral("first_source_fingerprint"), firstFingerprint},
                                      {QStringLiteral("second_source_fingerprint"), secondFingerprint},
                                      {QStringLiteral("matched"), matched}, {QStringLiteral("matching_frames"), matchingFrames},
                                      {QStringLiteral("first_match_seconds"), firstSeconds}, {QStringLiteral("second_match_seconds"), secondSeconds},
                                      {QStringLiteral("method"), QStringLiteral("FFmpeg MPEG-7 signature detectmode=full")}};

        QString error;
        if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(pairSourceId, combinedFingerprint,
                                                                  record.extractorId, record.extractorVersion,
                                                                  QList<VibeCutMediaEvidenceRecord>{record}, &error)) {
            jobs->markFailed(jobId, QStringLiteral("Similarity evidence persistence failed: %1").arg(error));
            process->deleteLater();
            return;
        }
        jobs->markSucceeded(jobId, matched ? QStringLiteral("MPEG-7 comparison found %1 matching frame(s).").arg(matchingFrames)
                                           : QStringLiteral("MPEG-7 comparison found no matching segment."));
        process->deleteLater();
    });

    QObject::connect(process, &QProcess::errorOccurred, tools, [jobs, process, jobId](QProcess::ProcessError processError) {
        if (processError == QProcess::Crashed) return;
        VibeCutJob job;
        if (jobs->job(jobId, job) && !job.terminal()) {
            jobs->markFailed(jobId, QStringLiteral("Could not start/run FFmpeg signature comparison: %1").arg(process->errorString()));
        }
    });

    process->start(ffmpeg, args);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job_id"), jobId},
                       {QStringLiteral("first_bin_id"), firstId}, {QStringLiteral("second_bin_id"), secondId},
                       {QStringLiteral("pair_source_id"), pairSourceId}, {QStringLiteral("source_fingerprint"), combinedFingerprint},
                       {QStringLiteral("extractor_id"), QString::fromLatin1(ExtractorId)},
                       {QStringLiteral("extractor_version"), QString::fromLatin1(ExtractorVersion)},
                       {QStringLiteral("asynchronous"), true}};
}
} // namespace

bool registerVibeCutSimilarityTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Similarity comparison requires the native VibeCutTools/JobManager surface.");
        return false;
    }
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("first_bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("second_bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("first_bin_id"), QStringLiteral("second_bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_similarity_compare")},
                             {QStringLiteral("description"), QStringLiteral("Asynchronously compare two distinct file-backed video bin assets using FFmpeg's MPEG-7 signature filter in full detect mode. Persist pairwise match/no-match evidence and matching-frame metadata without scanning the whole project or mutating it.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_similarity_compare");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    if (!surface.registerTool(schema, policy, [tools](const QJsonObject &input) { return startSimilarity(tools, input); }, error)) return false;
    if (!registerVibeCutSemanticTools(surface, error)) return false;
    if (!registerVibeCutCrossModalTools(surface, error)) return false;
    if (!registerVibeCutDuplicateFusionTools(surface, error)) return false;
    if (!registerVibeCutHybridSearchTools(surface, error)) return false;
    if (!registerVibeCutDuplicateCandidateTools(surface, error)) return false;
    if (!registerVibeCutRoughCutSynthesisTools(surface, error)) return false;
    if (!registerVibeCutRoughCutRelevanceTools(surface, error)) return false;
    if (!registerVibeCutRoughCutAlternativeTools(surface, error)) return false;
    if (!registerVibeCutHighlightTools(surface, error)) return false;
    if (!registerVibeCutBrollTools(surface, error)) return false;
    if (!registerVibeCutPacingTools(surface, error)) return false;
    if (!registerVibeCutNarrativeTools(surface, error)) return false;
    if (!registerVibeCutContinuityTools(surface, error)) return false;
    return registerVibeCutEditorialEvalTools(surface, error);
}