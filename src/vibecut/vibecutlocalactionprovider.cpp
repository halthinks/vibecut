/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutlocalactionprovider.h"

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
#include <QSet>
#include <QStandardPaths>
#include <QVector>
#include <QtMath>

#include <cmath>
#include <memory>

namespace {
const QString kProviderId = QStringLiteral("local_xclip_actions");
const QString kExtractorId = QStringLiteral("local_xclip_actions");
const QString kExtractorVersion = QStringLiteral("1.0.0");
const QString kModel = QStringLiteral("microsoft/xclip-base-patch32");
const QString kModelRevision = QStringLiteral("47627d79085e55e641829bd120ac64a3cc3c2238");
const QString kTaxonomy = QStringLiteral("VibeCutActionSet-v1");
const QString kActionSetSha256 = QStringLiteral("005794f327b4bbf0cea1dd3801009f1c9c51066fec0bb129b7a01b0f8d5520fc");
const QString kTransformersVersion = QStringLiteral("5.16.1");
const QString kTorchVersion = QStringLiteral("2.14.0");
constexpr int kFramesPerWindow = 8;
constexpr int kMaxWindows = 100;
constexpr int kMaxSampledFrames = 800;

struct ActionSpec { QString label; QString prompt; };
struct WindowSpec { int start = -1; int end = -1; QJsonArray observedFrames; };

const QVector<ActionSpec> &actionSet()
{
    static const QVector<ActionSpec> set{
        {QStringLiteral("no_clear_action"), QStringLiteral("a video with no clear action from the listed set")},
        {QStringLiteral("talking"), QStringLiteral("a video of a person talking")},
        {QStringLiteral("presenting"), QStringLiteral("a video of a person presenting to an audience")},
        {QStringLiteral("talking_to_camera"), QStringLiteral("a video of a person talking directly to the camera")},
        {QStringLiteral("walking"), QStringLiteral("a video of a person walking")},
        {QStringLiteral("running"), QStringLiteral("a video of a person running")},
        {QStringLiteral("sitting"), QStringLiteral("a video of a person sitting")},
        {QStringLiteral("standing"), QStringLiteral("a video of a person standing")},
        {QStringLiteral("driving"), QStringLiteral("a video of a person driving a vehicle")},
        {QStringLiteral("riding_bicycle"), QStringLiteral("a video of a person riding a bicycle")},
        {QStringLiteral("cooking"), QStringLiteral("a video of a person cooking")},
        {QStringLiteral("eating"), QStringLiteral("a video of a person eating")},
        {QStringLiteral("drinking"), QStringLiteral("a video of a person drinking")},
        {QStringLiteral("typing"), QStringLiteral("a video of a person typing on a keyboard")},
        {QStringLiteral("writing"), QStringLiteral("a video of a person writing")},
        {QStringLiteral("reading"), QStringLiteral("a video of a person reading")},
        {QStringLiteral("using_phone"), QStringLiteral("a video of a person using a phone")},
        {QStringLiteral("using_computer"), QStringLiteral("a video of a person using a computer")},
        {QStringLiteral("assembling"), QStringLiteral("a video of a person assembling something")},
        {QStringLiteral("repairing"), QStringLiteral("a video of a person repairing something")},
        {QStringLiteral("using_hand_tool"), QStringLiteral("a video of a person using a hand tool")},
        {QStringLiteral("lifting"), QStringLiteral("a video of a person lifting something")},
        {QStringLiteral("carrying"), QStringLiteral("a video of a person carrying something")},
        {QStringLiteral("opening"), QStringLiteral("a video of a person opening something")},
        {QStringLiteral("closing"), QStringLiteral("a video of a person closing something")},
        {QStringLiteral("entering"), QStringLiteral("a video of a person entering an area")},
        {QStringLiteral("exiting"), QStringLiteral("a video of a person exiting an area")},
        {QStringLiteral("pointing"), QStringLiteral("a video of a person pointing")},
        {QStringLiteral("gesturing"), QStringLiteral("a video of a person gesturing")},
        {QStringLiteral("dancing"), QStringLiteral("a video of a person dancing")},
        {QStringLiteral("exercising"), QStringLiteral("a video of a person exercising")},
        {QStringLiteral("throwing"), QStringLiteral("a video of a person throwing something")},
        {QStringLiteral("catching"), QStringLiteral("a video of a person catching something")},
        {QStringLiteral("cutting"), QStringLiteral("a video of a person cutting something")},
        {QStringLiteral("pouring"), QStringLiteral("a video of a person pouring something")},
        {QStringLiteral("cleaning"), QStringLiteral("a video of a person cleaning")},
        {QStringLiteral("loading_unloading"), QStringLiteral("a video of a person loading or unloading something")},
        {QStringLiteral("operating_machinery"), QStringLiteral("a video of a person operating machinery")},
        {QStringLiteral("demonstrating_product"), QStringLiteral("a video of a person demonstrating a product")},
        {QStringLiteral("inspecting"), QStringLiteral("a video of a person inspecting something")},
        {QStringLiteral("working_at_bench"), QStringLiteral("a video of a person working at a bench")},
        {QStringLiteral("welding"), QStringLiteral("a video of a person welding")},
        {QStringLiteral("drilling"), QStringLiteral("a video of a person drilling")},
        {QStringLiteral("hammering"), QStringLiteral("a video of a person hammering")},
        {QStringLiteral("fastening"), QStringLiteral("a video of a person fastening a screw or bolt")},
        {QStringLiteral("handling_vehicle_part"), QStringLiteral("a video of a person handling a vehicle part")},
        {QStringLiteral("handling_electronics"), QStringLiteral("a video of a person handling an electronic device")},
    };
    return set;
}

bool readDouble(const QJsonObject &parameters, const QString &name, double fallback,
                double minimum, double maximum, double &result, QString *error)
{
    if (!parameters.contains(name)) { result = fallback; return true; }
    const QJsonValue value = parameters.value(name);
    if (!value.isDouble()) { if (error) *error = QStringLiteral("Action parameter %1 must be numeric.").arg(name); return false; }
    result = value.toDouble();
    if (!std::isfinite(result) || result < minimum || result > maximum) {
        if (error) *error = QStringLiteral("Action parameter %1 must be finite and between %2 and %3.").arg(name).arg(minimum).arg(maximum);
        return false;
    }
    return true;
}

bool readInt(const QJsonObject &parameters, const QString &name, int fallback,
             int minimum, int maximum, int &result, QString *error)
{
    if (!parameters.contains(name)) { result = fallback; return true; }
    const QJsonValue value = parameters.value(name);
    const double raw = value.toDouble(static_cast<double>(minimum - 1));
    const int converted = value.toInt(minimum - 1);
    if (!value.isDouble() || !std::isfinite(raw) || static_cast<double>(converted) != raw || converted < minimum || converted > maximum) {
        if (error) *error = QStringLiteral("Action parameter %1 must be an integer from %2 to %3.").arg(name).arg(minimum).arg(maximum);
        return false;
    }
    result = converted;
    return true;
}

QJsonArray sampledFrames(int start, int end)
{
    QJsonArray frames;
    if (end - start < kFramesPerWindow) return frames;
    int previous = -1;
    for (int i = 0; i < kFramesPerWindow; ++i) {
        qint64 frame64 = static_cast<qint64>(start) + static_cast<qint64>(i) * (static_cast<qint64>(end) - start - 1) / (kFramesPerWindow - 1);
        int frame = static_cast<int>(frame64);
        if (previous >= 0 && frame <= previous) frame = previous + 1;
        if (frame >= end) return QJsonArray();
        frames.append(frame);
        previous = frame;
    }
    return frames;
}

QVector<WindowSpec> expectedWindows(int startFrame, int endFrame, int windowFrames, int hopFrames, int maxWindows, QString *error)
{
    QVector<WindowSpec> result;
    QSet<int> uniqueFrames;
    qint64 cursor = startFrame;
    while (cursor < endFrame) {
        const qint64 end64 = qMin<qint64>(endFrame, cursor + windowFrames);
        if (end64 - cursor < kFramesPerWindow) break;
        WindowSpec window;
        window.start = static_cast<int>(cursor);
        window.end = static_cast<int>(end64);
        window.observedFrames = sampledFrames(window.start, window.end);
        if (window.observedFrames.size() != kFramesPerWindow) {
            if (error) *error = QStringLiteral("Could not construct exactly eight unique source frames for an action window.");
            return {};
        }
        for (const QJsonValue &value : window.observedFrames) uniqueFrames.insert(value.toInt());
        result.append(window);
        if (result.size() > maxWindows || result.size() > kMaxWindows) {
            if (error) *error = QStringLiteral("Action range/cadence exceeds max_windows=%1.").arg(maxWindows);
            return {};
        }
        if (uniqueFrames.size() > kMaxSampledFrames) {
            if (error) *error = QStringLiteral("Action request exceeds the %1 unique sampled-frame safety limit.").arg(kMaxSampledFrames);
            return {};
        }
        cursor += hopFrames;
    }
    if (result.isEmpty() && error) *error = QStringLiteral("Action range produced no valid eight-frame windows.");
    return result;
}

class LocalXClipActionProvider : public VibeCutExtractorProvider
{
public:
    QString id() const override { return kProviderId; }
    QString displayName() const override { return QStringLiteral("Local Microsoft X-CLIP actions"); }
    QStringList capabilities() const override { return {QStringLiteral("actions")}; }

    bool configured(QString *error) const override
    {
        if (!vibeCutVisionDependenciesReady(error)) return false;
        const QString script = vibeCutActionScript();
        if (script.isEmpty() || !QFileInfo::exists(script)) {
            if (error) *error = QStringLiteral("VibeCut's installed X-CLIP action helper was not found.");
            return false;
        }
        return true;
    }

    QJsonObject start(const QString &capability, const QJsonObject &input,
                      const VibeCutExtractorProviderContext &context, QString *error) override
    {
        if (error) error->clear();
        if (capability.trimmed().toLower() != QLatin1String("actions")) { if (error) *error = QStringLiteral("Local X-CLIP only implements actions."); return QJsonObject(); }
        if (!context.jobs || !context.persistEvidence) { if (error) *error = QStringLiteral("Local X-CLIP requires the shared JobManager and validated evidence sink."); return QJsonObject(); }
        if (!input.value(QStringLiteral("has_video")).toBool(false)) { if (error) *error = QStringLiteral("Action prediction requires a source with video."); return QJsonObject(); }
        QString readyError;
        if (!configured(&readyError)) { if (error) *error = readyError; return QJsonObject(); }

        const QString sourcePath = input.value(QStringLiteral("source_path")).toString();
        const QString sourceId = input.value(QStringLiteral("source_id")).toString();
        const QString sourceFingerprint = input.value(QStringLiteral("source_fingerprint")).toString();
        const double fps = input.value(QStringLiteral("fps")).toDouble(0.0);
        const int startFrame = input.value(QStringLiteral("start_frame")).toInt(-1);
        const int endFrame = input.value(QStringLiteral("end_frame")).toInt(-1);
        if (sourcePath.isEmpty() || sourceId.isEmpty() || sourceFingerprint.isEmpty() || !std::isfinite(fps) || fps <= 0.0 || startFrame < 0 || endFrame <= startFrame) {
            if (error) *error = QStringLiteral("Local X-CLIP received an incomplete normalized extractor request.");
            return QJsonObject();
        }
        const QString ffmpeg = KdenliveSettings::ffmpegpath();
        if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) { if (error) *error = QStringLiteral("Kdenlive has no valid configured FFmpeg executable for action sampling."); return QJsonObject(); }

        const QJsonObject parameters = input.value(QStringLiteral("parameters")).toObject();
        if (parameters.contains(QStringLiteral("model")) || parameters.contains(QStringLiteral("revision")) || parameters.contains(QStringLiteral("labels")) || parameters.contains(QStringLiteral("prompts"))) {
            if (error) *error = QStringLiteral("The built-in action provider uses a fixed pinned model and VibeCutActionSet-v1; model/revision/labels/prompts are not caller-overridable.");
            return QJsonObject();
        }
        double windowSeconds = 4.0, hopSeconds = 2.0, minScore = 0.05;
        int maxWindows = 50, topK = 5;
        if (!readDouble(parameters, QStringLiteral("window_seconds"), 4.0, 0.5, 10.0, windowSeconds, error) ||
            !readDouble(parameters, QStringLiteral("hop_seconds"), 2.0, 0.25, 10.0, hopSeconds, error) ||
            !readDouble(parameters, QStringLiteral("min_score"), 0.05, 0.0, 1.0, minScore, error) ||
            !readInt(parameters, QStringLiteral("max_windows"), 50, 1, kMaxWindows, maxWindows, error) ||
            !readInt(parameters, QStringLiteral("top_k"), 5, 1, 10, topK, error)) return QJsonObject();
        if (hopSeconds > windowSeconds) { if (error) *error = QStringLiteral("Action hop_seconds may not exceed window_seconds."); return QJsonObject(); }
        const QString device = parameters.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
        if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) { if (error) *error = QStringLiteral("Action parameter device must be auto, cpu, or cuda."); return QJsonObject(); }

        const qint64 windowFrames64 = qMax<qint64>(kFramesPerWindow, qRound64(windowSeconds * fps));
        const qint64 hopFrames64 = qMax<qint64>(1, qRound64(hopSeconds * fps));
        if (windowFrames64 > std::numeric_limits<int>::max() || hopFrames64 > std::numeric_limits<int>::max()) { if (error) *error = QStringLiteral("Action window/hop frame conversion overflowed."); return QJsonObject(); }
        QString windowError;
        const QVector<WindowSpec> windows = expectedWindows(startFrame, endFrame, static_cast<int>(windowFrames64), static_cast<int>(hopFrames64), maxWindows, &windowError);
        if (windows.isEmpty()) { if (error) *error = windowError; return QJsonObject(); }

        const QStringList arguments{vibeCutActionScript(), QStringLiteral("--source"), sourcePath, QStringLiteral("--ffmpeg"), ffmpeg,
                                    QStringLiteral("--fps"), QString::number(fps, 'f', 9), QStringLiteral("--start-frame"), QString::number(startFrame),
                                    QStringLiteral("--end-frame"), QString::number(endFrame), QStringLiteral("--window-seconds"), QString::number(windowSeconds, 'f', 6),
                                    QStringLiteral("--hop-seconds"), QString::number(hopSeconds, 'f', 6), QStringLiteral("--max-windows"), QString::number(maxWindows),
                                    QStringLiteral("--top-k"), QString::number(topK), QStringLiteral("--min-score"), QString::number(minScore, 'f', 6),
                                    QStringLiteral("--device"), device, QStringLiteral("--model"), kModel, QStringLiteral("--revision"), kModelRevision};

        const QString jobId = context.jobs->createJob(QStringLiteral("visual_actions"), QStringLiteral("Action prediction · %1").arg(sourceId), true);
        context.jobs->markRunning(jobId, QStringLiteral("Running local X-CLIP action prediction…"));
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
                         [process, jobs = context.jobs, jobId, persistEvidence, sourceId, sourceFingerprint, windows, topK, minScore]
                         (int exitCode, QProcess::ExitStatus status) {
            VibeCutJob current;
            if (jobs->job(jobId, current) && current.state == VibeCutJobState::CancelRequested) { jobs->markCancelled(jobId, QStringLiteral("Action prediction cancelled.")); process->deleteLater(); return; }
            if (status != QProcess::NormalExit || exitCode != 0) { const QString e = QString::fromUtf8(process->readAllStandardError()).right(6000).trimmed(); jobs->markFailed(jobId, e.isEmpty() ? QStringLiteral("Local X-CLIP exited with code %1.").arg(exitCode) : e); process->deleteLater(); return; }
            const QByteArray stdoutData = process->readAllStandardOutput();
            if (stdoutData.size() > 32 * 1024 * 1024) { jobs->markFailed(jobId, QStringLiteral("Local X-CLIP output exceeded the 32 MiB safety limit.")); process->deleteLater(); return; }
            QJsonParseError parseError; const QJsonDocument doc = QJsonDocument::fromJson(stdoutData, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) { jobs->markFailed(jobId, QStringLiteral("Local X-CLIP returned malformed JSON: %1").arg(parseError.errorString())); process->deleteLater(); return; }
            const QJsonObject root = doc.object();
            const QString transformersVersion = root.value(QStringLiteral("transformers_version")).toString().trimmed();
            const QString torchVersion = root.value(QStringLiteral("torch_version")).toString().trimmed();
            if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 || root.value(QStringLiteral("authority")).toString() != QLatin1String("model_prediction") ||
                root.value(QStringLiteral("score_semantics")).toString() != QLatin1String("softmax_over_fixed_action_set") || root.value(QStringLiteral("taxonomy")).toString() != kTaxonomy ||
                root.value(QStringLiteral("action_set_sha256")).toString() != kActionSetSha256 || root.value(QStringLiteral("candidate_count")).toInt(-1) != actionSet().size() ||
                root.value(QStringLiteral("model")).toString() != kModel || root.value(QStringLiteral("model_revision")).toString() != kModelRevision ||
                root.value(QStringLiteral("model_license")).toString() != QLatin1String("MIT") || transformersVersion != kTransformersVersion || !torchVersion.startsWith(kTorchVersion) ||
                root.value(QStringLiteral("frames_per_window")).toInt(-1) != kFramesPerWindow || root.value(QStringLiteral("window_count")).toInt(-1) != windows.size() || !root.value(QStringLiteral("windows")).isArray()) {
                jobs->markFailed(jobId, QStringLiteral("Local X-CLIP returned an unsupported or provenance-mismatched result schema.")); process->deleteLater(); return;
            }
            const QJsonArray returnedWindows = root.value(QStringLiteral("windows")).toArray();
            if (returnedWindows.size() != windows.size()) { jobs->markFailed(jobId, QStringLiteral("Local X-CLIP did not cover the exact expected window sequence.")); process->deleteLater(); return; }
            QList<VibeCutMediaEvidenceRecord> records; int recordIndex = 0;
            for (int i = 0; i < returnedWindows.size(); ++i) {
                if (!returnedWindows.at(i).isObject()) { jobs->markFailed(jobId, QStringLiteral("Local X-CLIP returned a non-object window.")); process->deleteLater(); return; }
                const QJsonObject returned = returnedWindows.at(i).toObject(); const WindowSpec &expected = windows.at(i);
                if (returned.value(QStringLiteral("index")).toInt(-1) != i || returned.value(QStringLiteral("start_frame")).toInt(-1) != expected.start ||
                    returned.value(QStringLiteral("end_frame")).toInt(-1) != expected.end || returned.value(QStringLiteral("observed_frames")).toArray() != expected.observedFrames) {
                    jobs->markFailed(jobId, QStringLiteral("Local X-CLIP returned action-window provenance that does not match the authoritative expected frames.")); process->deleteLater(); return;
                }
                const QJsonArray predictions = returned.value(QStringLiteral("predictions")).toArray();
                if (predictions.size() > topK) { jobs->markFailed(jobId, QStringLiteral("Local X-CLIP returned more ranked predictions than requested.")); process->deleteLater(); return; }
                for (const QJsonValue &predictionValue : predictions) {
                    if (!predictionValue.isObject()) { jobs->markFailed(jobId, QStringLiteral("Local X-CLIP returned a non-object prediction.")); process->deleteLater(); return; }
                    const QJsonObject prediction = predictionValue.toObject();
                    const QJsonValue idValue = prediction.value(QStringLiteral("label_id")); const int labelId = idValue.toInt(-1); const double rawId = idValue.toDouble(-1.0);
                    const QJsonValue rankValue = prediction.value(QStringLiteral("rank")); const int rank = rankValue.toInt(-1); const double rawRank = rankValue.toDouble(-1.0);
                    const double score = prediction.value(QStringLiteral("score")).toDouble(-1.0); const QString label = prediction.value(QStringLiteral("label")).toString(); const QString prompt = prediction.value(QStringLiteral("prompt")).toString();
                    if (!idValue.isDouble() || labelId < 0 || labelId >= actionSet().size() || static_cast<double>(labelId) != rawId || !rankValue.isDouble() || rank < 1 || rank > topK || static_cast<double>(rank) != rawRank ||
                        !std::isfinite(score) || score < minScore - 0.000001 || score > 1.0 || label != actionSet().at(labelId).label || prompt != actionSet().at(labelId).prompt) {
                        jobs->markFailed(jobId, QStringLiteral("Local X-CLIP returned an invalid or action-set-mismatched prediction.")); process->deleteLater(); return;
                    }
                    VibeCutMediaEvidenceRecord record;
                    record.id = QStringLiteral("action:%1:%2:%3").arg(sourceId.mid(sourceId.indexOf(QLatin1Char(':')) + 1), sourceFingerprint.left(12)).arg(recordIndex++);
                    record.sourceId = sourceId; record.sourceFingerprint = sourceFingerprint; record.extractorId = kExtractorId; record.extractorVersion = kExtractorVersion;
                    record.kind = QStringLiteral("action_prediction"); record.startFrame = expected.start; record.endFrame = expected.end;
                    record.text = QStringLiteral("X-CLIP VibeCutActionSet prediction: %1 (relative score %2)").arg(label).arg(score, 0, 'f', 4); record.confidence = score;
                    record.metadata = QJsonObject{{QStringLiteral("label"), label}, {QStringLiteral("prompt"), prompt}, {QStringLiteral("label_id"), labelId}, {QStringLiteral("rank"), rank},
                                                  {QStringLiteral("window_start_frame"), expected.start}, {QStringLiteral("window_end_frame"), expected.end}, {QStringLiteral("observed_frames"), expected.observedFrames},
                                                  {QStringLiteral("model"), kModel}, {QStringLiteral("model_revision"), kModelRevision}, {QStringLiteral("model_license"), QStringLiteral("MIT")},
                                                  {QStringLiteral("taxonomy"), kTaxonomy}, {QStringLiteral("action_set_sha256"), kActionSetSha256}, {QStringLiteral("candidate_count"), actionSet().size()},
                                                  {QStringLiteral("authority"), QStringLiteral("model_prediction")}, {QStringLiteral("score_semantics"), QStringLiteral("softmax_over_fixed_action_set")},
                                                  {QStringLiteral("device"), root.value(QStringLiteral("device")).toString()}, {QStringLiteral("transformers_version"), transformersVersion}, {QStringLiteral("torch_version"), torchVersion},
                                                  {QStringLiteral("window_seconds"), root.value(QStringLiteral("window_seconds")).toDouble()}, {QStringLiteral("hop_seconds"), root.value(QStringLiteral("hop_seconds")).toDouble()}};
                    records.append(record);
                }
            }
            QString persistError;
            if (!persistEvidence(sourceId, sourceFingerprint, kExtractorId, kExtractorVersion, records, &persistError)) { jobs->markFailed(jobId, QStringLiteral("Action evidence was rejected: %1").arg(persistError)); process->deleteLater(); return; }
            jobs->markSucceeded(jobId, QStringLiteral("Persisted %1 X-CLIP action prediction(s) across %2 window(s).").arg(records.size()).arg(windows.size())); process->deleteLater();
        });
        QObject::connect(process, &QProcess::errorOccurred, process, [process, jobs = context.jobs, jobId](QProcess::ProcessError processError) {
            if (processError != QProcess::FailedToStart) return; jobs->markFailed(jobId, QStringLiteral("Could not launch the configured VibeCut vision Python environment for X-CLIP.")); process->deleteLater();
        });
        process->start(vibeCutVisionPython(), arguments);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true}, {QStringLiteral("job_id"), jobId}, {QStringLiteral("model"), kModel},
                           {QStringLiteral("model_revision"), kModelRevision}, {QStringLiteral("taxonomy"), kTaxonomy}, {QStringLiteral("action_set_sha256"), kActionSetSha256},
                           {QStringLiteral("score_semantics"), QStringLiteral("softmax_over_fixed_action_set")}, {QStringLiteral("window_count"), windows.size()},
                           {QStringLiteral("window_seconds"), windowSeconds}, {QStringLiteral("hop_seconds"), hopSeconds}, {QStringLiteral("top_k"), topK}, {QStringLiteral("device"), device}};
    }
};
}

QString vibeCutActionScript()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/vibecut/action_xclip.py"));
}

void ensureVibeCutLocalActionProviderRegistered()
{
    static bool registered = false;
    if (registered) return;
    QString error;
    if (VibeCutExtractorProviderRegistry::global().registerProvider(kProviderId, []() { return std::make_unique<LocalXClipActionProvider>(); }, &error)) { registered = true; return; }
    if (VibeCutExtractorProviderRegistry::global().providerIds().contains(kProviderId)) registered = true;
}
