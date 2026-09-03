/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutextractorprovidertools.h"

#include "vibecutaudioeventsetuptools.h"
#include "vibecudaudioeventsummary.h"
#include "vibecutdiarizationsetuptools.h"
#include "vibecutextractorevidencecontract.h"
#include "vibecutextractorprovider.h"
#include "vibecutextractorrequest.h"
#include "vibecutjobmanager.h"
#include "vibecutlocalactionprovider.h"
#include "vibecutlocalaudioeventprovider.h"
#include "vibecutlocaldiarizationprovider.h"
#include "vibecutlocalobjectprovider.h"
#include "vibecutlocalocrprovider.h"
#include "vibecutmediaevidence.h"
#include "vibecutobjecttracks.h"
#include "vibecutocrtemporal.h"
#include "vibecutspeakeridentitytools.h"
#include "vibecutsubjectcandidates.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"
#include "vibecutvisionsetuptools.h"

#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject integerProperty(int minimum, int maximum = -1)
{
    QJsonObject property{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), minimum}};
    if (maximum >= minimum) property.insert(QStringLiteral("maximum"), maximum);
    return property;
}

QJsonObject numberProperty(double minimum, double maximum)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                       {QStringLiteral("minimum"), minimum}, {QStringLiteral("maximum"), maximum}};
}

QJsonObject deviceProperty()
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}};
}

QJsonObject boundedSourceSchema(const QJsonObject &extraProperties)
{
    QJsonObject properties{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                           {QStringLiteral("start_frame"), integerProperty(0)},
                           {QStringLiteral("end_frame"), integerProperty(0)}};
    for (auto it = extraProperties.constBegin(); it != extraProperties.constEnd(); ++it) properties.insert(it.key(), it.value());
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                       {QStringLiteral("additionalProperties"), false}};
}

QJsonObject listProviders(const QJsonObject &input)
{
    const QString capability = input.value(QStringLiteral("capability")).toString().trimmed().toLower();
    VibeCutExtractorProviderRegistry &registry = VibeCutExtractorProviderRegistry::global();
    const QStringList ids = capability.isEmpty() ? registry.providerIds() : registry.providerIdsForCapability(capability);
    QJsonArray providers;
    for (const QString &id : ids) {
        QString createError;
        std::unique_ptr<VibeCutExtractorProvider> provider = registry.create(id, &createError);
        if (!provider) continue;
        QString configuredError;
        const bool configured = provider->configured(&configuredError);
        QJsonArray capabilities;
        for (const QString &value : provider->capabilities()) capabilities.append(value);
        providers.append(QJsonObject{{QStringLiteral("id"), provider->id()},
                                     {QStringLiteral("display_name"), provider->displayName()},
                                     {QStringLiteral("capabilities"), capabilities},
                                     {QStringLiteral("configured"), configured},
                                     {QStringLiteral("configuration_error"), configuredError}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("capability_filter"), capability},
                       {QStringLiteral("provider_count"), providers.size()}, {QStringLiteral("providers"), providers}};
}

QJsonObject startProvider(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools) return err(QStringLiteral("Native VibeCutTools/JobManager is unavailable."));
    QString persistReadyError;
    if (!VibeCutMediaEvidence::canPersistCurrent(&persistReadyError)) return err(persistReadyError);

    const QString providerId = input.value(QStringLiteral("provider_id")).toString().trimmed();
    const QString capability = input.value(QStringLiteral("capability")).toString().trimmed().toLower();
    const QJsonObject request = input.value(QStringLiteral("request")).toObject();
    if (providerId.isEmpty() || capability.isEmpty()) return err(QStringLiteral("provider_id and capability must not be empty."));

    QString createError;
    std::unique_ptr<VibeCutExtractorProvider> provider = VibeCutExtractorProviderRegistry::global().create(providerId, &createError);
    if (!provider) return err(createError);
    QStringList capabilities = provider->capabilities();
    for (QString &value : capabilities) value = value.trimmed().toLower();
    if (!capabilities.contains(capability)) return err(QStringLiteral("Extractor provider '%1' does not declare capability '%2'.").arg(providerId, capability));

    QString configuredError;
    if (!provider->configured(&configuredError)) {
        return err(configuredError.isEmpty() ? QStringLiteral("Extractor provider '%1' is not configured.").arg(providerId) : configuredError);
    }

    QJsonObject normalizedRequest;
    QString normalizeError;
    if (!normalizeVibeCutExtractorRequest(capability, request, normalizedRequest, &normalizeError)) return err(normalizeError);
    const int requestedStartFrame = normalizedRequest.value(QStringLiteral("start_frame")).toInt(-1);
    const int requestedEndFrame = normalizedRequest.value(QStringLiteral("end_frame")).toInt(-1);

    VibeCutExtractorProviderContext context;
    context.jobs = tools->jobManager();
    context.persistEvidence = [capability, requestedStartFrame, requestedEndFrame](const QString &sourceId,
                                                                                  const QString &sourceFingerprint,
                                                                                  const QString &extractorId,
                                                                                  const QString &extractorVersion,
                                                                                  const QList<VibeCutMediaEvidenceRecord> &records,
                                                                                  QString *error) {
        QString contractError;
        if (!validateVibeCutExtractorEvidenceContract(capability, requestedStartFrame, requestedEndFrame, records, &contractError)) {
            if (error) *error = contractError;
            return false;
        }
        return VibeCutMediaEvidence::replaceSourceExtractorCurrent(sourceId, sourceFingerprint, extractorId, extractorVersion, records, error);
    };

    QString startError;
    QJsonObject result = provider->start(capability, normalizedRequest, context, &startError);
    if (!startError.isEmpty()) return err(startError);
    if (!result.contains(QStringLiteral("ok"))) result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("provider_id"), providerId);
    result.insert(QStringLiteral("capability"), capability);
    result.insert(QStringLiteral("source_id"), normalizedRequest.value(QStringLiteral("source_id")));
    result.insert(QStringLiteral("source_fingerprint"), normalizedRequest.value(QStringLiteral("source_fingerprint")));
    return result;
}

bool registerFirstClassProvider(VibeCutToolSurface &surface,
                                VibeCutTools *tools,
                                const QString &toolName,
                                const QString &providerId,
                                const QString &capability,
                                const QString &description,
                                const QJsonObject &inputSchema,
                                QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = toolName;
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    const QJsonObject schema{{QStringLiteral("name"), toolName},
                             {QStringLiteral("description"), description},
                             {QStringLiteral("input_schema"), inputSchema}};
    return surface.registerTool(schema, policy,
                                [tools, providerId, capability](const QJsonObject &input) {
                                    return startProvider(tools, QJsonObject{{QStringLiteral("provider_id"), providerId},
                                                                           {QStringLiteral("capability"), capability},
                                                                           {QStringLiteral("request"), input}});
                                }, error);
}
}

bool registerVibeCutExtractorProviderTools(VibeCutToolSurface &surface, QString *error)
{
    ensureVibeCutBuiltinExtractorProvidersRegistered();
    ensureVibeCutLocalOcrProviderRegistered();
    ensureVibeCutLocalAudioEventProviderRegistered();
    ensureVibeCutLocalObjectProviderRegistered();
    ensureVibeCutLocalActionProviderRegistered();

    const QJsonObject listInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{{QStringLiteral("capability"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("extractor_providers_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), listPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("List built-in and externally registered media-extractor providers and their declared capabilities/configuration state, optionally filtered by capability such as ocr, diarization, audio_events, objects or actions.")},
                                          {QStringLiteral("input_schema"), listInput}},
                              listPolicy, listProviders, error)) return false;

    VibeCutTools *tools = surface.baseTools();
    if (!tools) { if (error) *error = QStringLiteral("Extractor provider tools require native VibeCutTools/JobManager."); return false; }

    const QJsonObject genericInput{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{
                                       {QStringLiteral("provider_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                       {QStringLiteral("capability"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                       {QStringLiteral("request"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("provider_id"), QStringLiteral("capability"), QStringLiteral("request")}},
                                   {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy genericPolicy;
    genericPolicy.name = QStringLiteral("extractor_provider_start");
    genericPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    genericPolicy.asynchronous = true;
    genericPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), genericPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Start an explicitly registered extractor through authoritative source normalization, shared JobManager, capability-specific evidence admission and the bounded evidence ledger. Providers do not receive a caller-invented source path or generic evidence-write escape hatch.")},
                                          {QStringLiteral("input_schema"), genericInput}},
                              genericPolicy, [tools](const QJsonObject &input) { return startProvider(tools, input); }, error)) return false;

    const QJsonObject ocrSchema = boundedSourceSchema(QJsonObject{
        {QStringLiteral("sample_interval_frames"), integerProperty(1, 1000000)},
        {QStringLiteral("max_samples"), integerProperty(1, 2000)},
        {QStringLiteral("language"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 128}}},
        {QStringLiteral("psm"), integerProperty(3, 13)},
        {QStringLiteral("min_confidence"), numberProperty(0.0, 1.0)},
    });
    if (!registerFirstClassProvider(surface, tools, QStringLiteral("media_ocr_refresh"), QStringLiteral("local_tesseract"), QStringLiteral("ocr"),
                                    QStringLiteral("Run bounded local Tesseract OCR on authoritative sampled source frames and persist one-frame text observations with confidence/geometry/language/engine provenance."),
                                    ocrSchema, error)) return false;

    const QJsonObject audioSchema = boundedSourceSchema(QJsonObject{
        {QStringLiteral("window_seconds"), numberProperty(1.0, 10.0)},
        {QStringLiteral("hop_seconds"), numberProperty(0.25, 10.0)},
        {QStringLiteral("max_windows"), integerProperty(1, 500)},
        {QStringLiteral("top_k"), integerProperty(1, 20)},
        {QStringLiteral("min_score"), numberProperty(0.0, 1.0)},
        {QStringLiteral("device"), deviceProperty()},
    });
    if (!registerFirstClassProvider(surface, tools, QStringLiteral("media_audio_events_refresh"), QStringLiteral("local_ast_audioset"), QStringLiteral("audio_events"),
                                    QStringLiteral("Run bounded local MIT AST AudioSet classification. Persist ranked model-prediction windows with taxonomy/model provenance; hop_seconds may not exceed window_seconds and predictions are not observed facts."),
                                    audioSchema, error)) return false;

    const QJsonObject objectSchema = boundedSourceSchema(QJsonObject{
        {QStringLiteral("sample_interval_frames"), integerProperty(1, 1000000)},
        {QStringLiteral("max_samples"), integerProperty(1, 1000)},
        {QStringLiteral("max_detections_per_frame"), integerProperty(1, 100)},
        {QStringLiteral("min_score"), numberProperty(0.0, 1.0)},
        {QStringLiteral("device"), deviceProperty()},
    });
    if (!registerFirstClassProvider(surface, tools, QStringLiteral("media_objects_refresh"), QStringLiteral("local_detr_coco"), QStringLiteral("objects"),
                                    QStringLiteral("Run pinned local DETR COCO detection over an exact bounded frame sample sequence. Persist one-frame object model predictions with score, model/taxonomy provenance and bounded pixel geometry; predictions are not identity or continuous observations."),
                                    objectSchema, error)) return false;

    const QJsonObject actionSchema = boundedSourceSchema(QJsonObject{
        {QStringLiteral("window_seconds"), numberProperty(0.5, 10.0)},
        {QStringLiteral("hop_seconds"), numberProperty(0.25, 10.0)},
        {QStringLiteral("max_windows"), integerProperty(1, 100)},
        {QStringLiteral("top_k"), integerProperty(1, 10)},
        {QStringLiteral("min_score"), numberProperty(0.0, 1.0)},
        {QStringLiteral("device"), deviceProperty()},
    });
    if (!registerFirstClassProvider(surface, tools, QStringLiteral("media_actions_refresh"), QStringLiteral("local_xclip_actions"), QStringLiteral("actions"),
                                    QStringLiteral("Run pinned local Microsoft X-CLIP over bounded video windows using the fixed VibeCutActionSet-v1. Every ranked model prediction retains the exact eight observed source frames and softmax-over-fixed-set score semantics; callers cannot supply models, labels or prompts."),
                                    actionSchema, error)) return false;

    if (!registerVibeCutOcrTemporalTools(surface, error)) return false;
    if (!registerVibeCutAudioEventSummaryTools(surface, error)) return false;
    if (!registerVibeCutObjectTrackTools(surface, error)) return false;
    if (!registerVibeCutSubjectCandidateTools(surface, error)) return false;
    if (!registerVibeCutDiarizationSetupTools(surface, error)) return false;
    if (!registerVibeCutAudioEventSetupTools(surface, error)) return false;
    if (!registerVibeCutVisionSetupTools(surface, error)) return false;
    return registerVibeCutSpeakerIdentityTools(surface, error);
}
