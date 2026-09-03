/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutdiarizationsetuptools.h"

#include "vibecutextractorprovider.h"
#include "vibecutjobmanager.h"
#include "vibecutlocaldiarizationprovider.h"
#include "vibecutsecretstore.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {
const QString kModel = QStringLiteral("pyannote/speaker-diarization-community-1");
const QString kPinnedPackage = QStringLiteral("pyannote.audio==4.0.7");

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString requirementsPath()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                  QStringLiteral("scripts/vibecut/requirements-diarization.txt"));
}

QString environmentTokenSource()
{
    if (!qEnvironmentVariableIsEmpty("HF_TOKEN")) return QStringLiteral("HF_TOKEN");
    if (!qEnvironmentVariableIsEmpty("HUGGINGFACE_TOKEN")) return QStringLiteral("HUGGINGFACE_TOKEN");
    return QString();
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

void startDependencyInstall(VibeCutJobManager *jobs, const QString &jobId, const QString &python, const QString &requirements)
{
    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    bindCancellation(jobs, process, jobId);
    jobs->setProgress(jobId, 45, QStringLiteral("Installing pinned pyannote.audio runtime…"));

    QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process,
                     [process, jobs, jobId](int exitCode, QProcess::ExitStatus status) {
                         if (markCancelledIfRequested(jobs, jobId, QStringLiteral("Speaker diarization setup cancelled."))) {
                             process->deleteLater();
                             return;
                         }
                         if (status != QProcess::NormalExit || exitCode != 0) {
                             const QString output = QString::fromUtf8(process->readAll()).right(5000).trimmed();
                             jobs->markFailed(jobId, output.isEmpty()
                                                         ? QStringLiteral("Installing the pinned pyannote runtime failed with code %1.").arg(exitCode)
                                                         : output);
                             process->deleteLater();
                             return;
                         }
                         QString readyError;
                         if (!vibeCutPyannoteDependenciesReady(&readyError)) {
                             jobs->markFailed(jobId, QStringLiteral("Package installation completed but runtime verification failed: %1").arg(readyError));
                             process->deleteLater();
                             return;
                         }
                         jobs->setProgress(jobId, 100, QStringLiteral("Local speaker diarization runtime is ready."));
                         jobs->markSucceeded(jobId, QStringLiteral("Installed and verified %1. Model acquisition happens on the first diarization run after a Hugging Face token is configured.").arg(kPinnedPackage));
                         process->deleteLater();
                     });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, jobs, jobId](QProcess::ProcessError processError) {
                         if (processError != QProcess::FailedToStart) return;
                         jobs->markFailed(jobId, QStringLiteral("Could not launch the diarization Python environment for package installation."));
                         process->deleteLater();
                     });
    process->start(python, {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"),
                            QStringLiteral("--disable-pip-version-check"), QStringLiteral("-r"), requirements});
}

QJsonObject status(const QJsonObject &)
{
    ensureVibeCutBuiltinExtractorProvidersRegistered();
    QString dependencyError;
    const bool dependenciesReady = vibeCutPyannoteDependenciesReady(&dependencyError);
    QString tokenError;
    const bool tokenConfigured = !vibeCutPyannoteToken(&tokenError).isEmpty();
    const QString tokenSource = environmentTokenSource();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("provider_id"), QStringLiteral("local_pyannote")},
                       {QStringLiteral("model"), kModel},
                       {QStringLiteral("pinned_package"), kPinnedPackage},
                       {QStringLiteral("python"), vibeCutPyannotePython()},
                       {QStringLiteral("script"), vibeCutPyannoteScript()},
                       {QStringLiteral("requirements"), requirementsPath()},
                       {QStringLiteral("dependencies_ready"), dependenciesReady},
                       {QStringLiteral("dependency_error"), dependenciesReady ? QString() : dependencyError},
                       {QStringLiteral("token_configured"), tokenConfigured},
                       {QStringLiteral("token_source"), tokenSource.isEmpty() ? (tokenConfigured ? QStringLiteral("kwallet") : QStringLiteral("none")) : tokenSource},
                       {QStringLiteral("kwallet_available"), VibeCutSecretStore::available()},
                       {QStringLiteral("ready"), dependenciesReady && tokenConfigured},
                       {QStringLiteral("note"), QStringLiteral("Credentials are never returned. Configure HF_TOKEN/HUGGINGFACE_TOKEN in the application environment or store the token through a native KWallet settings surface; VibeCut chat plans do not accept secret token input.")}};
}

QJsonObject setup(VibeCutTools *tools, const QJsonObject &)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("Shared VibeCut JobManager is unavailable."));
    const QString requirements = requirementsPath();
    if (requirements.isEmpty() || !QFileInfo::exists(requirements)) {
        return err(QStringLiteral("Pinned VibeCut diarization requirements file is not installed."));
    }
    if (!qEnvironmentVariableIsEmpty("VIBECUT_PYANNOTE_PYTHON")) {
        return err(QStringLiteral("VIBECUT_PYANNOTE_PYTHON points to a user-managed Python environment. VibeCut will not modify an externally managed interpreter; install %1 there or unset the override and run setup again.").arg(kPinnedPackage));
    }

    QString readyError;
    if (vibeCutPyannoteDependenciesReady(&readyError)) {
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("started"), false},
                           {QStringLiteral("already_ready"), true},
                           {QStringLiteral("pinned_package"), kPinnedPackage}};
    }

    VibeCutJobManager *jobs = tools->jobManager();
    const QString jobId = jobs->createJob(QStringLiteral("diarization_setup"),
                                          QStringLiteral("Install local speaker diarization runtime"), true);
    jobs->markRunning(jobId, QStringLiteral("Preparing isolated pyannote Python environment…"));
    jobs->setProgress(jobId, 5);

    const QString venvPython = vibeCutPyannotePython();
    if (QFileInfo::exists(venvPython)) {
        startDependencyInstall(jobs, jobId, venvPython, requirements);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                           {QStringLiteral("job_id"), jobId}, {QStringLiteral("stage"), QStringLiteral("install_dependencies")}};
    }

    const QString systemPython = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (systemPython.isEmpty()) {
        jobs->markFailed(jobId, QStringLiteral("python3 is not available on PATH."));
        return err(QStringLiteral("python3 is required to create the isolated diarization environment."));
    }
    QDir().mkpath(QFileInfo(vibeCutPyannoteVenvDir()).absolutePath());

    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    bindCancellation(jobs, process, jobId);
    QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process,
                     [process, jobs, jobId, venvPython, requirements](int exitCode, QProcess::ExitStatus status) {
                         if (markCancelledIfRequested(jobs, jobId, QStringLiteral("Speaker diarization setup cancelled."))) {
                             process->deleteLater();
                             return;
                         }
                         if (status != QProcess::NormalExit || exitCode != 0 || !QFileInfo::exists(venvPython)) {
                             const QString output = QString::fromUtf8(process->readAll()).right(5000).trimmed();
                             jobs->markFailed(jobId, output.isEmpty()
                                                         ? QStringLiteral("Creating the diarization virtual environment failed with code %1.").arg(exitCode)
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
                         jobs->markFailed(jobId, QStringLiteral("Could not launch python3 to create the diarization environment."));
                         process->deleteLater();
                     });
    process->start(systemPython, {QStringLiteral("-m"), QStringLiteral("venv"), vibeCutPyannoteVenvDir()});

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), jobId}, {QStringLiteral("stage"), QStringLiteral("create_venv")}};
}

QJsonObject startDiarization(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("VibeCut tool surface is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("speaker_diarization_start requires bin_id."));

    QJsonObject request{{QStringLiteral("bin_id"), binId}};
    for (const QString &name : {QStringLiteral("start_frame"), QStringLiteral("end_frame"),
                                QStringLiteral("exclusive"), QStringLiteral("device"),
                                QStringLiteral("min_speakers"), QStringLiteral("max_speakers")}) {
        if (input.contains(name)) request.insert(name, input.value(name));
    }
    return surface->invoke(QStringLiteral("extractor_provider_start"),
                           QJsonObject{{QStringLiteral("provider_id"), QStringLiteral("local_pyannote")},
                                       {QStringLiteral("capability"), QStringLiteral("diarization")},
                                       {QStringLiteral("request"), request}});
}
}

bool registerVibeCutDiarizationSetupTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("speaker_diarization_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), statusPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Report the built-in local pyannote diarization runtime, pinned package/model, dependency readiness and whether a Hugging Face token is available, without returning credentials.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              statusPolicy, status, error)) return false;

    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Diarization setup requires native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolPolicy setupPolicy;
    setupPolicy.name = QStringLiteral("speaker_diarization_setup");
    setupPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    setupPolicy.asynchronous = true;
    setupPolicy.confirmationRequired = true;
    setupPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), setupPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Create a VibeCut-owned isolated Python environment and install the pinned local speaker-diarization runtime. This downloads/install packages, is cancellable through JobManager, always requires confirmation, and never accepts or exposes credentials.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              setupPolicy, [tools](const QJsonObject &input) { return setup(tools, input); }, error)) return false;

    const QJsonObject startInput{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{
                                     {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                     {QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                     {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
                                     {QStringLiteral("exclusive"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("default"), true}}},
                                     {QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                            {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}}},
                                     {QStringLiteral("min_speakers"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 20}}},
                                     {QStringLiteral("max_speakers"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 20}}}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                 {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy startPolicy;
    startPolicy.name = QStringLiteral("speaker_diarization_start");
    startPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    startPolicy.asynchronous = true;
    startPolicy.mutatesProject = false;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), startPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Start bounded local speaker diarization for an authoritative file-backed bin asset using VibeCut's built-in local_pyannote provider. Source path/fingerprint are resolved internally; results remain anonymous speaker clusters until separately user-identified.")},
                                            {QStringLiteral("input_schema"), startInput}},
                                startPolicy, [&surface](const QJsonObject &input) { return startDiarization(&surface, input); }, error);
}
