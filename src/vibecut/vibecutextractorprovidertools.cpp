/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutextractorprovidertools.h"

#include "vibecutaudioeventsetuptools.h"
#include "vibecudaudioeventsummary.h"
#include "vibecutdiarizationsetuptools.h"
#include "vibecutextractorevidencecontract.h"
#include "vibecutextractorprovider.h"
#include "vibecutextractorrequest.h"
#include "vibecutjobmanager.h"
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
        QJsonArray caps;
        for (const QString &cap : provider->capabilities()) caps.append(cap);
        providers.append(QJsonObject{{QStringLiteral("id"), provider->id()},
                                     {QStringLiteral("display_name"), provider->displayName()},
                                     {QStringLiteral("capabilities"), caps},
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

    QStringList caps = provider->capabilities();
    for (QString &cap : caps) cap = cap.trimmed().toLower();
    if (!caps.contains(capability)) {
        return err(QStringLiteral("Extractor provider '%1' does not declare capability '%2'.").arg(providerId, capability));
    }
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

QJsonObject startLocalOcr(VibeCutTools *tools, const QJsonObject &input)
{
    return startProvider(tools,
                         QJsonObject{{QStringLiteral("provider_id"), QStringLiteral("local_tesseract")},
                                     {QStringLiteral("capability"), QStringLiteral("ocr")},
                                     {QStringLiteral("request"), input}});
}

QJsonObject startLocalAudioEvents(VibeCutTools *tools, const QJsonObject &input)
{
    return startProvider(tools,
                         QJsonObject{{QStringLiteral("provider_id"), QStringLiteral("local_ast_audioset")},
                                     {QStringLiteral("capability"), QStringLiteral("audio_events")},
                                     {QStringLiteral("request"), input}});
}

QJsonObject startLocalObjects(VibeCutTools *tools, const QJsonObject &input)
{
    return startProvider(tools,
                         QJsonObject{{QStringLiteral("provider_id"), QStringLiteral("local_detr_coco")},
                                     {QStringLiteral("capability"), QStringLiteral("objects")},
                                     {QStringLiteral("request"), input}});
}
} // namespace

bool registerVibeCutExtractorProviderTools(VibeCutToolSurface &surface, QString *error)
{
    ensureVibeCutBuiltinExtractorProvidersRegistered();
    ensureVibeCutLocalOcrProviderRegistered();
    ensureVibeCutLocalAudioEventProviderRegistered();
    ensureVibeCutLocalObjectProviderRegistered();

    const QJsonObject listInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{{QStringLiteral("capability"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("extractor_providers_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), listPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("List built-in and externally registered media-extractor providers and their declared capabilities/configuration state, optionally filtered by capability such as ocr, diarization, audio_events, objects, embeddings, actions, or faces.")},
                                          {QStringLiteral("input_schema"), listInput}},
                              listPolicy, listProviders, error)) return false;

    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Extractor provider start requires the native VibeCutTools/JobManager surface.");
        return false;
    }
    const QJsonObject startInput{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{
                                     {QStringLiteral("provider_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                     {QStringLiteral("capability"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                     {QStringLiteral("request"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                              {QStringLiteral("description"), QStringLiteral("Provider-neutral request. Must contain bin_id; optional start_frame/end_frame and provider parameters are normalized against live Kdenlive source state before dispatch.")}}}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("provider_id"), QStringLiteral("capability"), QStringLiteral("request")}},
                                 {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy startPolicy;
    startPolicy.name = QStringLiteral("extractor_provider_start");
    startPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    startPolicy.asynchronous = true;
    startPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), startPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Start one explicitly registered model-backed media extractor capability through normalized authoritative source metadata, the shared VibeCut JobManager, capability-specific evidence contracts and the validated evidence sink. The provider never receives a caller-invented source path or generic evidence-write escape hatch.")},
                                          {QStringLiteral("input_schema"), startInput}},
                              startPolicy, [tools](const QJsonObject &input) { return startProvider(tools, input); }, error)) return false;

    const QJsonObject ocrInput{{QStringLiteral("type"), QStringLiteral("object")},
                               {QStringLiteral("properties"), QJsonObject{
                                   {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                   {QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                   {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                   {QStringLiteral("sample_interval_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000000}}},
                                   {QStringLiteral("max_samples"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 2000}}},
                                   {QStringLiteral("language"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 128}}},
                                   {QStringLiteral("psm"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 3}, {QStringLiteral("maximum"), 13}}},
                                   {QStringLiteral("min_confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}}}},
                               {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                               {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy ocrPolicy;
    ocrPolicy.name = QStringLiteral("media_ocr_refresh");
    ocrPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    ocrPolicy.asynchronous = true;
    ocrPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), ocrPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Run the built-in local Tesseract OCR provider over authoritative bounded frames from one file-backed video bin asset. Persists one-frame ocr_text evidence with normalized confidence, pixel bounding boxes, language and engine provenance through the validated media-evidence sink. Sampling is bounded and cancellable through JobManager.")},
                                          {QStringLiteral("input_schema"), ocrInput}},
                              ocrPolicy, [tools](const QJsonObject &input) { return startLocalOcr(tools, input); }, error)) return false;

    const QJsonObject audioEventInput{{QStringLiteral("type"), QStringLiteral("object")},
                                      {QStringLiteral("properties"), QJsonObject{
                                          {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                          {QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                          {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                          {QStringLiteral("window_seconds"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 1.0}, {QStringLiteral("maximum"), 10.0}}},
                                          {QStringLiteral("hop_seconds"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.25}, {QStringLiteral("maximum"), 10.0}}},
                                          {QStringLiteral("max_windows"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 500}}},
                                          {QStringLiteral("top_k"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 20}}},
                                          {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                          {QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                                 {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}}}}},
                                      {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                      {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy audioEventPolicy;
    audioEventPolicy.name = QStringLiteral("media_audio_events_refresh");
    audioEventPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    audioEventPolicy.asynchronous = true;
    audioEventPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), audioEventPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Run the built-in local MIT AST AudioSet classifier over a bounded source excerpt. Persists ranked audio_event_prediction records with exact source-frame windows, model/taxonomy provenance and normalized scores. Predictions are not promoted to observed facts. Work is bounded and cancellable through JobManager; hop_seconds may not exceed window_seconds.")},
                                          {QStringLiteral("input_schema"), audioEventInput}},
                              audioEventPolicy, [tools](const QJsonObject &input) { return startLocalAudioEvents(tools, input); }, error)) return false;

    const QJsonObject objectInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("sample_interval_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000000}}},
                                      {QStringLiteral("max_samples"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}},
                                      {QStringLiteral("max_detections_per_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}}},
                                      {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                      {QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                             {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy objectPolicy;
    objectPolicy.name = QStringLiteral("media_objects_refresh");
    objectPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    objectPolicy.asynchronous = true;
    objectPolicy.mutatesProject = false;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), objectPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Run the built-in pinned DETR COCO object detector over an exact bounded arithmetic sample sequence from one file-backed video bin asset. Persists one-frame object_detection_prediction evidence with score, label/model/taxonomy provenance and bounded pixel geometry. Predictions are not identity or continuous-observation claims; work is bounded and cancellable through JobManager.")},
                                          {QStringLiteral("input_schema"), objectInput}},
                              objectPolicy, [tools](const QJsonObject &input) { return startLocalObjects(tools, input); }, error)) return false;

    if (!registerVibeCutOcrTemporalTools(surface, error)) return false;
    if (!registerVibeCutAudioEventSummaryTools(surface, error)) return false;
    if (!registerVibeCutObjectTrackTools(surface, error)) return false;
    if (!registerVibeCutSubjectCandidateTools(surface, error)) return false;
    if (!registerVibeCutDiarizationSetupTools(surface, error)) return false;
    if (!registerVibeCutAudioEventSetupTools(surface, error)) return false;
    if (!registerVibeCutVisionSetupTools(surface, error)) return false;
    return registerVibeCutSpeakerIdentityTools(surface, error);
}
