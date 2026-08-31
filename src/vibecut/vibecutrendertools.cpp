/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutrendertools.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "render/renderrequest.h"
#include "renderpresets/renderpresetmodel.hpp"
#include "renderpresets/renderpresetrepository.hpp"
#include "vibecutjobmanager.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>

#include <vector>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

class RenderExecution : public QObject
{
public:
    RenderExecution(VibeCutTools *tools, const QString &jobId, std::vector<RenderRequest::RenderJob> jobs, QObject *parent)
        : QObject(parent)
        , m_tools(tools)
        , m_jobId(jobId)
        , m_jobs(std::move(jobs))
    {
        if (m_tools && m_tools->jobManager()) {
            connect(m_tools->jobManager(), &VibeCutJobManager::jobChanged, this, [this](const QString &changedId) {
                if (changedId != m_jobId || !m_process) return;
                VibeCutJob job;
                if (m_tools->jobManager()->job(m_jobId, job) && job.state == VibeCutJobState::CancelRequested) {
                    m_cancelRequested = true;
                    m_process->kill();
                }
            });
        }
    }

    void start() { startNext(); }

private:
    void cleanupTemporaryInputs()
    {
        for (const RenderRequest::RenderJob &job : m_jobs) {
            if (!job.playlistPath.isEmpty()) QFile::remove(job.playlistPath);
            if (!job.subtitlePath.isEmpty()) QFile::remove(job.subtitlePath);
        }
    }

    QString outputSummary() const
    {
        QStringList outputs;
        for (const RenderRequest::RenderJob &job : m_jobs) {
            if (!job.outputFile.isEmpty() && !outputs.contains(job.outputFile)) outputs.append(job.outputFile);
        }
        return outputs.join(QStringLiteral(", "));
    }

    bool outputsVerified(QString &error) const
    {
        QStringList outputs;
        for (const RenderRequest::RenderJob &job : m_jobs) {
            if (!job.outputFile.isEmpty() && !outputs.contains(job.outputFile)) outputs.append(job.outputFile);
        }
        for (const QString &output : outputs) {
            QFileInfo info(output);
            if (!info.exists() || !info.isFile() || info.size() <= 0) {
                error = QStringLiteral("Render process exited successfully but output is missing or empty: %1").arg(output);
                return false;
            }
        }
        return !outputs.isEmpty();
    }

    void finishFailed(const QString &message)
    {
        cleanupTemporaryInputs();
        if (m_tools && m_tools->jobManager()) m_tools->jobManager()->markFailed(m_jobId, message);
        deleteLater();
    }

    void finishCancelled()
    {
        cleanupTemporaryInputs();
        if (m_tools && m_tools->jobManager()) m_tools->jobManager()->markCancelled(m_jobId, QStringLiteral("Render cancelled."));
        deleteLater();
    }

    void startNext()
    {
        if (!m_tools || !m_tools->jobManager()) {
            deleteLater();
            return;
        }
        if (m_index >= static_cast<int>(m_jobs.size())) {
            QString verificationError;
            if (!outputsVerified(verificationError)) {
                finishFailed(verificationError.isEmpty() ? QStringLiteral("Render produced no verifiable output files.") : verificationError);
                return;
            }
            cleanupTemporaryInputs();
            m_tools->jobManager()->markSucceeded(m_jobId, QStringLiteral("Render completed and output verified: %1").arg(outputSummary()));
            deleteLater();
            return;
        }

        const RenderRequest::RenderJob current = m_jobs.at(static_cast<size_t>(m_index));
        m_process = new QProcess(this);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        if (!KdenliveSettings::hwDecoding().isEmpty()) {
            environment.insert(QStringLiteral("MLT_AVFORMAT_HWACCEL"), KdenliveSettings::hwDecoding());
        }
        m_process->setProcessEnvironment(environment);
        m_process->setProgram(KdenliveSettings::kdenliverendererpath());
        m_process->setArguments(RenderRequest::argsByJob(current, false));
        m_process->setProcessChannelMode(QProcess::MergedChannels);

        const int total = static_cast<int>(m_jobs.size());
        const int baseline = total > 0 ? (m_index * 100) / total : 0;
        m_tools->jobManager()->setProgress(m_jobId, baseline,
                                           QStringLiteral("Rendering pass/job %1 of %2 to %3")
                                               .arg(m_index + 1).arg(total).arg(current.outputFile));

        connect(m_process, &QProcess::readyRead, this, [this]() {
            if (!m_process || !m_tools || !m_tools->jobManager()) return;
            const QString output = QString::fromUtf8(m_process->readAll()).trimmed();
            if (!output.isEmpty()) m_lastOutput = (m_lastOutput + QLatin1Char('\n') + output).right(3000);
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart && !m_cancelRequested) {
                finishFailed(QStringLiteral("Could not launch Kdenlive renderer: %1").arg(m_process ? m_process->errorString() : QStringLiteral("unknown error")));
            }
        });
        connect(m_process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (!m_process) return;
                    const QString tail = QString::fromUtf8(m_process->readAll()).trimmed();
                    if (!tail.isEmpty()) m_lastOutput = (m_lastOutput + QLatin1Char('\n') + tail).right(3000);
                    m_process->deleteLater();
                    m_process = nullptr;
                    if (m_cancelRequested) {
                        finishCancelled();
                        return;
                    }
                    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                        finishFailed(QStringLiteral("Kdenlive renderer failed (exit %1): %2")
                                         .arg(exitCode)
                                         .arg(m_lastOutput.trimmed().isEmpty() ? QStringLiteral("no renderer output") : m_lastOutput.trimmed()));
                        return;
                    }
                    ++m_index;
                    startNext();
                });
        m_process->start();
    }

    VibeCutTools *m_tools = nullptr;
    QString m_jobId;
    std::vector<RenderRequest::RenderJob> m_jobs;
    int m_index = 0;
    QProcess *m_process = nullptr;
    bool m_cancelRequested = false;
    QString m_lastOutput;
};

QJsonObject listPresets(const QJsonObject &)
{
    QJsonArray presets;
    for (const QString &name : RenderPresetRepository::get()->getAllPresets()) {
        if (!RenderPresetRepository::get()->presetExists(name)) continue;
        std::unique_ptr<RenderPresetModel> &preset = RenderPresetRepository::get()->getPreset(name);
        if (!preset) continue;
        presets.append(QJsonObject{{QStringLiteral("name"), preset->name()},
                                   {QStringLiteral("group"), preset->groupId()},
                                   {QStringLiteral("extension"), preset->extension()},
                                   {QStringLiteral("valid"), preset->isValid()},
                                   {QStringLiteral("error"), preset->error()},
                                   {QStringLiteral("warning"), preset->warning()}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("presets"), presets}};
}

void cleanupPreparedJobs(const std::vector<RenderRequest::RenderJob> &jobs)
{
    for (const RenderRequest::RenderJob &job : jobs) {
        if (!job.playlistPath.isEmpty()) QFile::remove(job.playlistPath);
        if (!job.subtitlePath.isEmpty()) QFile::remove(job.subtitlePath);
    }
}

QJsonObject startRender(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    if (!pCore || !pCore->currentDoc()) return err(QStringLiteral("No project is open."));
    if (pCore->projectDuration() < 2) return err(QStringLiteral("The timeline is empty; add clips before rendering."));
    if (!QFile::exists(KdenliveSettings::meltpath())) return err(QStringLiteral("Cannot find the configured MLT melt executable required for rendering."));
    if (!QFile::exists(KdenliveSettings::kdenliverendererpath())) return err(QStringLiteral("Cannot find Kdenlive's configured render helper executable."));

    const QString presetName = input.value(QStringLiteral("preset")).toString().trimmed();
    if (presetName.isEmpty() || !RenderPresetRepository::get()->presetExists(presetName)) {
        return err(QStringLiteral("Unknown render preset '%1'. Call render_presets_list first.").arg(presetName));
    }
    std::unique_ptr<RenderPresetModel> &preset = RenderPresetRepository::get()->getPreset(presetName);
    if (!preset || !preset->isValid() || !preset->error().isEmpty()) {
        return err(QStringLiteral("Render preset '%1' is not usable: %2").arg(presetName, preset ? preset->error() : QStringLiteral("missing preset model")));
    }

    QString outputFile = input.value(QStringLiteral("output_file")).toString().trimmed();
    if (outputFile.isEmpty()) return err(QStringLiteral("output_file must not be empty"));
    QFileInfo outputInfo(outputFile);
    if (outputInfo.isRelative()) {
        outputFile = QDir(pCore->currentDoc()->projectRenderFolder()).absoluteFilePath(outputFile);
        outputInfo.setFile(outputFile);
    }
    QDir outputDir = outputInfo.dir();
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        return err(QStringLiteral("Could not create output directory: %1").arg(outputDir.absolutePath()));
    }
    const bool overwrite = input.value(QStringLiteral("overwrite")).toBool(false);
    const bool outputExists = QFile::exists(outputFile);
    if (outputExists && !overwrite) {
        return err(QStringLiteral("Output file already exists. Set overwrite=true only when the user explicitly approved replacement: %1").arg(outputFile));
    }

    RenderRequest request;
    request.setOutputFile(outputFile);
    request.loadPresetParams(presetName);
    request.setProxyRendering(input.value(QStringLiteral("use_proxies")).toBool(false));
    request.setEmbedSubtitles(input.value(QStringLiteral("embed_subtitles")).toBool(false));
    request.setTwoPass(input.value(QStringLiteral("two_pass")).toBool(false));
    const int inFrame = input.contains(QStringLiteral("in_frame")) ? input.value(QStringLiteral("in_frame")).toInt(-1) : -1;
    const int outFrame = input.contains(QStringLiteral("out_frame")) ? input.value(QStringLiteral("out_frame")).toInt(-1) : -1;
    if (inFrame >= 0 || outFrame >= 0) request.setBounds(inFrame, outFrame);

    std::vector<RenderRequest::RenderJob> jobs = request.process();
    if (!request.errorMessages().isEmpty()) {
        cleanupPreparedJobs(jobs);
        return err(QStringLiteral("Render preparation failed: %1").arg(request.errorMessages().join(QStringLiteral("; "))));
    }
    if (jobs.empty()) return err(QStringLiteral("Render preparation produced no jobs."));

    // Preserve the existing output until Kdenlive has successfully generated
    // the renderer jobs/playlist. Only an explicit approved overwrite removes
    // it, immediately before the renderer process begins.
    if (outputExists && overwrite && !QFile::remove(outputFile)) {
        cleanupPreparedJobs(jobs);
        return err(QStringLiteral("Render jobs were prepared, but the approved existing output could not be removed: %1").arg(outputFile));
    }

    const QString jobId = tools->jobManager()->createJob(QStringLiteral("render"), QStringLiteral("Render %1").arg(QFileInfo(outputFile).fileName()), true);
    tools->jobManager()->markRunning(jobId, QStringLiteral("Prepared %1 Kdenlive render job(s).").arg(jobs.size()));
    RenderExecution *execution = new RenderExecution(tools, jobId, std::move(jobs), tools);
    execution->start();

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true}, {QStringLiteral("job_id"), jobId},
                       {QStringLiteral("preset"), presetName}, {QStringLiteral("output_file"), outputFile},
                       {QStringLiteral("note"), QStringLiteral("Rendering is running through Kdenlive's native RenderRequest/kdenlive_render path and will be verified on completion.")}};
}
} // namespace

bool registerVibeCutRenderTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("render tools require a native VibeCut runtime");
        return false;
    }
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{{QStringLiteral("name"), QStringLiteral("render_presets_list")},
                                 {QStringLiteral("description"), QStringLiteral("List the actual render presets installed in this Kdenlive runtime with extension/validity/warnings. Read-only; use before render_start instead of inventing preset names.")},
                                 {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("render_presets_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy, listPresets, error)) return false;

    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("preset"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("output_file"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("overwrite"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                                      {QStringLiteral("in_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("out_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("use_proxies"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                                      {QStringLiteral("embed_subtitles"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                                      {QStringLiteral("two_pass"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("preset"), QStringLiteral("output_file")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject startSchema{{QStringLiteral("name"), QStringLiteral("render_start")},
                                  {QStringLiteral("description"), QStringLiteral("Render the active project/range with an installed Kdenlive preset through RenderRequest and kdenlive_render. Async, cancellable, and final output files are verified. Relative output paths resolve under the project's render folder; overwrite requires explicit true.")},
                                  {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy renderPolicy;
    renderPolicy.name = QStringLiteral("render_start");
    renderPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    renderPolicy.asynchronous = true;
    renderPolicy.mutatesProject = false;
    return surface.registerTool(startSchema, renderPolicy, [tools](const QJsonObject &input) { return startRender(tools, input); }, error);
}
