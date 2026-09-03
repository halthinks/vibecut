/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutextractorprovidertools.h"

#include "vibecutextractorevidencecontract.h"
#include "vibecutextractorprovider.h"
#include "vibecutextractorrequest.h"
#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

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
} // namespace

bool registerVibeCutExtractorProviderTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject listInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{{QStringLiteral("capability"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("extractor_providers_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), listPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("List externally registered media-extractor providers and their declared capabilities/configuration state, optionally filtered by capability such as ocr, diarization, embeddings, objects, faces, or audio_events.")},
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
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), startPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Start one explicitly registered model-backed media extractor capability through normalized authoritative source metadata, the shared VibeCut JobManager, capability-specific evidence contracts and the validated evidence sink. The provider never receives a caller-invented source path or generic evidence-write escape hatch.")},
                                            {QStringLiteral("input_schema"), startInput}},
                                startPolicy, [tools](const QJsonObject &input) { return startProvider(tools, input); }, error);
}
