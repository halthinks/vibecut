/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutvisionsetuptools.h"

#include "vibecutjobmanager.h"
#include "vibecutlocalactionprovider.h"
#include "vibecutlocalobjectprovider.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"
#include "vibecutvisionruntime.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {
const QString kDetrModel = QStringLiteral("facebook/detr-resnet-50");
const QString kDetrRevision = QStringLiteral("ebd66332d81f2ee6d9fbfefd0235026b46a381d0");
const QString kXclipModel = QStringLiteral("microsoft/xclip-base-patch32");
const QString kXclipRevision = QStringLiteral("47627d79085e55e641829bd120ac64a3cc3c2238");
const QString kActionTaxonomy = QStringLiteral("VibeCutActionSet-v1");
const QString kActionSetSha256 = QStringLiteral("005794f327b4bbf0cea1dd3801009f1c9c51066fec0bb129b7a01b0f8d5520fc");
const QString kPinnedTransformers = QStringLiteral("transformers==5.16.1");
const QString kPinnedTorch = QStringLiteral("torch==2.14.0");
const QString kPinnedTorchvision = QStringLiteral("torchvision==0.29.0");
const QString kPinnedPillow = QStringLiteral("Pillow==12.3.0");

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
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

void startDependencyInstall(VibeCutJobManager *jobs, const QString &jobId,
                            const QString &python, const QString &requirements)
{
    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    bindCancellation(jobs, process, jobId);
    jobs->setProgress(jobId, 40, QStringLiteral("Installing pinned VibeCut vision runtime…"));

    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId](int exitCode, QProcess::ExitStatus status) {
        if (markCancelledIfRequested(jobs, jobId, QStringLiteral("Vision runtime setup cancelled."))) {
            process->deleteLater();
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString output = QString::fromUtf8(process->readAll()).right(8000).trimmed();
            jobs->markFailed(jobId, output.isEmpty()
                                        ? QStringLiteral("Installing the pinned vision runtime failed with code %1.").arg(exitCode)
                                        : output);
            process->deleteLater();
            return;
        }
        QString readyError;
        if (!vibeCutVisionDependenciesReady(&readyError)) {
            jobs->markFailed(jobId, QStringLiteral("Vision package installation completed but runtime verification failed: %1").arg(readyError));
            process->deleteLater();
            return;
        }
        jobs->setProgress(jobId, 100, QStringLiteral("Local VibeCut vision runtime is ready."));
        jobs->markSucceeded(jobId,
                            QStringLiteral("Installed and verified %1, %2, %3 and %4. Pinned DETR and X-CLIP model acquisition occurs on first use if not cached; their outputs remain model-prediction evidence, not observed fact.")
                                .arg(kPinnedTransformers, kPinnedTorch, kPinnedTorchvision, kPinnedPillow));
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, jobs, jobId](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart) return;
        jobs->markFailed(jobId, QStringLiteral("Could not launch the vision Python environment for package installation."));
        process->deleteLater();
    });
    process->start(python, {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"),
                            QStringLiteral("--disable-pip-version-check"), QStringLiteral("-r"), requirements});
}

QJsonObject status(const QJsonObject &)
{
    ensureVibeCutLocalObjectProviderRegistered();
    ensureVibeCutLocalActionProviderRegistered();

    QString dependencyError;
    const bool dependenciesReady = vibeCutVisionDependenciesReady(&dependencyError);
    const QString objectScript = vibeCutObjectDetectionScript();
    const QString actionScript = vibeCutActionScript();
    const bool objectHelperReady = !objectScript.isEmpty() && QFileInfo::exists(objectScript);
    const bool actionHelperReady = !actionScript.isEmpty() && QFileInfo::exists(actionScript);

    QJsonArray providers;
    providers.append(QJsonObject{{QStringLiteral("provider_id"), QStringLiteral("local_detr_coco")},
                                 {QStringLiteral("capability"), QStringLiteral("objects")},
                                 {QStringLiteral("taxonomy"), QStringLiteral("COCO-2017")},
                                 {QStringLiteral("model"), kDetrModel},
                                 {QStringLiteral("model_revision"), kDetrRevision},
                                 {QStringLiteral("helper"), objectScript},
                                 {QStringLiteral("helper_ready"), objectHelperReady},
                                 {QStringLiteral("ready"), dependenciesReady && objectHelperReady}});
    providers.append(QJsonObject{{QStringLiteral("provider_id"), QStringLiteral("local_xclip_actions")},
                                 {QStringLiteral("capability"), QStringLiteral("actions")},
                                 {QStringLiteral("taxonomy"), kActionTaxonomy},
                                 {QStringLiteral("model"), kXclipModel},
                                 {QStringLiteral("model_revision"), kXclipRevision},
                                 {QStringLiteral("model_license"), QStringLiteral("MIT")},
                                 {QStringLiteral("score_semantics"), QStringLiteral("softmax_over_fixed_action_set")},
                                 {QStringLiteral("action_set_sha256"), kActionSetSha256},
                                 {QStringLiteral("helper"), actionScript},
                                 {QStringLiteral("helper_ready"), actionHelperReady},
                                 {QStringLiteral("ready"), dependenciesReady && actionHelperReady}});

    return QJsonObject{{QStringLiteral("ok"), true},
                       // Compatibility fields for callers written against the first DETR-only status shape.
                       {QStringLiteral("provider_id"), QStringLiteral("local_detr_coco")},
                       {QStringLiteral("capability"), QStringLiteral("objects")},
                       {QStringLiteral("taxonomy"), QStringLiteral("COCO-2017")},
                       {QStringLiteral("model"), kDetrModel},
                       {QStringLiteral("model_revision"), kDetrRevision},
                       {QStringLiteral("providers"), providers},
                       {QStringLiteral("provider_count"), providers.size()},
                       {QStringLiteral("pinned_transformers"), kPinnedTransformers},
                       {QStringLiteral("pinned_torch"), kPinnedTorch},
                       {QStringLiteral("pinned_torchvision"), kPinnedTorchvision},
                       {QStringLiteral("pinned_pillow"), kPinnedPillow},
                       {QStringLiteral("python"), vibeCutVisionPython()},
                       {QStringLiteral("requirements"), vibeCutVisionRequirements()},
                       {QStringLiteral("object_helper"), objectScript},
                       {QStringLiteral("action_helper"), actionScript},
                       {QStringLiteral("dependencies_ready"), dependenciesReady},
                       {QStringLiteral("object_helper_ready"), objectHelperReady},
                       {QStringLiteral("action_helper_ready"), actionHelperReady},
                       {QStringLiteral("dependency_error"), dependenciesReady ? QString() : dependencyError},
                       {QStringLiteral("object_ready"), dependenciesReady && objectHelperReady},
                       {QStringLiteral("action_ready"), dependenciesReady && actionHelperReady},
                       {QStringLiteral("ready"), dependenciesReady && objectHelperReady && actionHelperReady},
                       {QStringLiteral("model_acquisition"), QStringLiteral("first_run_if_not_cached")},
                       {QStringLiteral("note"), QStringLiteral("The vision environment is isolated from diarization and audio-event runtimes. DETR emits sampled-frame object predictions with exact geometry. X-CLIP emits eight-frame zero-shot action predictions whose scores are relative to a fixed versioned action set. Neither output is promoted to observed fact, continuous observation, or human identity.")}};
}

QJsonObject setup(VibeCutTools *tools, const QJsonObject &)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("Shared VibeCut JobManager is unavailable."));
    const QString requirements = vibeCutVisionRequirements();
    if (requirements.isEmpty() || !QFileInfo::exists(requirements)) {
        return err(QStringLiteral("Pinned VibeCut vision requirements file is not installed."));
    }
    if (!qEnvironmentVariableIsEmpty("VIBECUT_VISION_PYTHON")) {
        return err(QStringLiteral("VIBECUT_VISION_PYTHON points to a user-managed Python environment. VibeCut will not modify an externally managed interpreter; install %1, %2, %3 and %4 there or unset the override and run setup again.")
                       .arg(kPinnedTransformers, kPinnedTorch, kPinnedTorchvision, kPinnedPillow));
    }

    QString readyError;
    if (vibeCutVisionDependenciesReady(&readyError)) {
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("started"), false},
                           {QStringLiteral("already_ready"), true},
                           {QStringLiteral("pinned_transformers"), kPinnedTransformers},
                           {QStringLiteral("pinned_torch"), kPinnedTorch},
                           {QStringLiteral("pinned_torchvision"), kPinnedTorchvision},
                           {QStringLiteral("pinned_pillow"), kPinnedPillow}};
    }

    VibeCutJobManager *jobs = tools->jobManager();
    const QString jobId = jobs->createJob(QStringLiteral("vision_setup"),
                                          QStringLiteral("Install local VibeCut vision runtime"), true);
    jobs->markRunning(jobId, QStringLiteral("Preparing isolated VibeCut vision Python environment…"));
    jobs->setProgress(jobId, 5);

    const QString venvPython = vibeCutVisionPython();
    if (QFileInfo::exists(venvPython)) {
        startDependencyInstall(jobs, jobId, venvPython, requirements);
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                           {QStringLiteral("job_id"), jobId}, {QStringLiteral("stage"), QStringLiteral("install_dependencies")}};
    }

    const QString systemPython = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (systemPython.isEmpty()) {
        jobs->markFailed(jobId, QStringLiteral("python3 is not available on PATH."));
        return err(QStringLiteral("python3 is required to create the isolated vision environment."));
    }
    QDir().mkpath(QFileInfo(vibeCutVisionVenvDir()).absolutePath());

    auto *process = new QProcess(jobs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    bindCancellation(jobs, process, jobId);
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                     [process, jobs, jobId, venvPython, requirements](int exitCode, QProcess::ExitStatus status) {
        if (markCancelledIfRequested(jobs, jobId, QStringLiteral("Vision runtime setup cancelled."))) {
            process->deleteLater();
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0 || !QFileInfo::exists(venvPython)) {
            const QString output = QString::fromUtf8(process->readAll()).right(8000).trimmed();
            jobs->markFailed(jobId, output.isEmpty()
                                        ? QStringLiteral("Creating the vision virtual environment failed with code %1.").arg(exitCode)
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
        jobs->markFailed(jobId, QStringLiteral("Could not launch python3 to create the vision environment."));
        process->deleteLater();
    });
    process->start(systemPython, {QStringLiteral("-m"), QStringLiteral("venv"), vibeCutVisionVenvDir()});

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), jobId}, {QStringLiteral("stage"), QStringLiteral("create_venv")}};
}
} // namespace

bool registerVibeCutVisionSetupTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};

    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("vision_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), statusPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Report the isolated local VibeCut vision runtime and both built-in vision providers: DETR sampled-frame object detection and X-CLIP fixed-taxonomy action prediction, including pinned package/model provenance and readiness. No vision prediction is promoted to observed fact or human identity.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              statusPolicy, status, error)) return false;

    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Vision setup requires native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolPolicy setupPolicy;
    setupPolicy.name = QStringLiteral("vision_setup");
    setupPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    setupPolicy.asynchronous = true;
    setupPolicy.confirmationRequired = true;
    setupPolicy.mutatesProject = false;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), setupPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Create a VibeCut-owned isolated Python environment and install the pinned local vision runtime shared by DETR object detection and X-CLIP action prediction. This can download large Torch/Transformers packages, is cancellable through JobManager and always requires confirmation.")},
                                            {QStringLiteral("input_schema"), noArgs}},
                                setupPolicy, [tools](const QJsonObject &input) { return setup(tools, input); }, error);
}
