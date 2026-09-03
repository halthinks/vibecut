/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutaudioeventsetuptools.h"

#include "vibecutjobmanager.h"
#include "vibecutlocalaudioeventprovider.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {
const QString kModel = QStringLiteral("MIT/ast-finetuned-audioset-10-10-0.4593");
const QString kPinnedTransformers = QStringLiteral("transformers==5.16.1");
const QString kPinnedTorch = QStringLiteral("torch==2.14.0");

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString requirementsPath()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                  QStringLiteral("scripts/vibecut/requirements-audio-events.txt"));
}

void bindCancellation(VibeCutJobManager *jobs, QProcess *process, const QString &jobId)
{
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, process,
                     [jobs, process, jobId](const QString &changedId) {
                         if (changedId != jobId || process->state() == QProcess::NotRunning) return;
                         VibeCutJob job;
                         if (!jobs->job(jobId, job)) return;
                         if (job.state == VibeCutJobState::CancelRequested) process->terminate();
                     });
}

bool markCancelledIfRequested(VibeCutJobManager *jobs, const QString &jobId, const QString &message)
{
    VibeCutJob job;
    if (!jobs->job(jobId, job) || job.state != VibeCutJobState::CancelRequested) return false;
    jobs->markCancelled(jobId, message);
    return true;
}

void startDependencyInstall(VibeCutJobManager *jobs,
                            const QString &jobId,
                            const QString &python,
                            const QString &requirements)
{
    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    bindCancellation(jobs, process, jobId);
    jobs->setProgress(jobId, 40, QStringLiteral("Installing pinned Transformers/Torch audio-event runtime…"));

    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId](int exitCode, QProcess::ExitStatus status) {
        if (markCancelledIfRequested(jobs, jobId, QStringLiteral("Audio-event runtime setup cancelled."))) {
            process->deleteLater();
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString output = QString::fromUtf8(process->readAll()).right(8000).trimmed();
            jobs->markFailed(jobId, output.isEmpty()
                                        ? QStringLiteral("Installing the pinned audio-event runtime failed with code %1.").arg(exitCode)
                                        : output);
            process->deleteLater();
            return;
        }
        QString readyError;
        if (!vibeCutAudioEventDependenciesReady(&readyError)) {
            jobs->markFailed(jobId, QStringLiteral("Package installation completed but runtime verification failed: %1").arg(readyError));
            process->deleteLater();
            return;
        }
        jobs->setProgress(jobId, 100, QStringLiteral("Local AudioSet event-classification runtime is ready."));
        jobs->markSucceeded(jobId,
                            QStringLiteral("Installed and verified %1 with %2. The pinned AST model is acquired on the first classification run and its output remains model-prediction evidence, not observed fact.")
                                .arg(kPinnedTransformers, kPinnedTorch));
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, jobs, jobId](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart) return;
        jobs->markFailed(jobId, QStringLiteral("Could not launch the audio-event Python environment for package installation."));
        process->deleteLater();
    });
    process->start(python, {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"),
                            QStringLiteral("--disable-pip-version-check"), QStringLiteral("-r"), requirements});
}

QJsonObject status(const QJsonObject &)
{
    ensureVibeCutLocalAudioEventProviderRegistered();
    QString dependencyError;
    const bool dependenciesReady = vibeCutAudioEventDependenciesReady(&dependencyError);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("provider_id"), QStringLiteral("local_ast_audioset")},
                       {QStringLiteral("capability"), QStringLiteral("audio_events")},
                       {QStringLiteral("taxonomy"), QStringLiteral("AudioSet")},
                       {QStringLiteral("model"), kModel},
                       {QStringLiteral("pinned_transformers"), kPinnedTransformers},
                       {QStringLiteral("pinned_torch"), kPinnedTorch},
                       {QStringLiteral("python"), vibeCutAudioEventPython()},
                       {QStringLiteral("script"), vibeCutAudioEventScript()},
                       {QStringLiteral("requirements"), requirementsPath()},
                       {QStringLiteral("dependencies_ready"), dependenciesReady},
                       {QStringLiteral("dependency_error"), dependenciesReady ? QString() : dependencyError},
                       {QStringLiteral("ready"), dependenciesReady},
                       {QStringLiteral("model_acquisition"), QStringLiteral("first_run_if_not_cached")},
                       {QStringLiteral("note"), QStringLiteral("The local runtime is isolated from diarization. A public Hugging Face model download may occur on first use. Classifier results are ranked model predictions and are admitted through the audio_events evidence contract.")}};
}

QJsonObject setup(VibeCutTools *tools, const QJsonObject &)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("Shared VibeCut JobManager is unavailable."));
    const QString requirements = requirementsPath();
    if (requirements.isEmpty() || !QFileInfo::exists(requirements)) {
        return err(QStringLiteral("Pinned VibeCut audio-event requirements file is not installed."));
    }
    if (!qEnvironmentVariableIsEmpty("VIBECUT_AUDIO_EVENTS_PYTHON")) {
        return err(QStringLiteral("VIBECUT_AUDIO_EVENTS_PYTHON points to a user-managed Python environment. VibeCut will not modify an externally managed interpreter; install %1 and %2 there or unset the override and run setup again.")
                       .arg(kPinnedTransformers, kPinnedTorch));
    }

    QString readyError;
    if (vibeCutAudioEventDependenciesReady(&readyError)) {
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("started"), false},
                           {QStringLiteral("already_ready"), true},
                           {QStringLiteral("pinned_transformers"), kPinnedTransformers},
                           {QStringLiteral("pinned_torch"), kPinnedTorch}};
    }

    VibeCutJobManager *jobs = tools->jobManager();
    const QString jobId = jobs->createJob(QStringLiteral("audio_event_setup"),
                                          QStringLiteral("Install local AudioSet event runtime"), true);
    jobs->markRunning(jobId, QStringLiteral("Preparing isolated audio-event Python environment…"));
    jobs->setProgress(jobId, 5);

    const QString venvPython = vibeCutAudioEventPython();
    if (QFileInfo::exists(venvPython)) {
        startDependencyInstall(jobs, jobId, venvPython, requirements);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                           {QStringLiteral("job_id"), jobId}, {QStringLiteral("stage"), QStringLiteral("install_dependencies")}};
    }

    const QString systemPython = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (systemPython.isEmpty()) {
        jobs->markFailed(jobId, QStringLiteral("python3 is not available on PATH."));
        return err(QStringLiteral("python3 is required to create the isolated audio-event environment."));
    }
    QDir().mkpath(QFileInfo(vibeCutAudioEventVenvDir()).absolutePath());

    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    bindCancellation(jobs, process, jobId);
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId, venvPython, requirements](int exitCode, QProcess::ExitStatus status) {
        if (markCancelledIfRequested(jobs, jobId, QStringLiteral("Audio-event runtime setup cancelled."))) {
            process->deleteLater();
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0 || !QFileInfo::exists(venvPython)) {
            const QString output = QString::fromUtf8(process->readAll()).right(8000).trimmed();
            jobs->markFailed(jobId, output.isEmpty()
                                        ? QStringLiteral("Creating the audio-event virtual environment failed with code %1.").arg(exitCode)
                                        : output);
            process->deleteLater();
            return;
        }
        process->deleteLater();
        startDependencyInstall(jobs, jobId, venvPython, requirements);
    });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, jobs, jobId](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart) return;
        jobs->markFailed(jobId, QStringLiteral("Could not launch python3 to create the audio-event environment."));
        process->deleteLater();
    });
    process->start(systemPython, {QStringLiteral("-m"), QStringLiteral("venv"), vibeCutAudioEventVenvDir()});

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), jobId}, {QStringLiteral("stage"), QStringLiteral("create_venv")}};
}
} // namespace

bool registerVibeCutAudioEventSetupTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};

    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("audio_event_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), statusPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Report the built-in local MIT AST AudioSet runtime, pinned package/model versions and dependency readiness. No model prediction is treated as observed fact.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              statusPolicy, status, error)) return false;

    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Audio-event setup requires native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolPolicy setupPolicy;
    setupPolicy.name = QStringLiteral("audio_event_setup");
    setupPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    setupPolicy.asynchronous = true;
    setupPolicy.confirmationRequired = true;
    setupPolicy.mutatesProject = false;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), setupPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Create a VibeCut-owned isolated Python environment and install the pinned local AudioSet event-classification runtime. This can download large Torch/Transformers packages, is cancellable through JobManager and always requires confirmation.")},
                                            {QStringLiteral("input_schema"), noArgs}},
                                setupPolicy, [tools](const QJsonObject &input) { return setup(tools, input); }, error);
}
