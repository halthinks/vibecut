/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutmediaevidence.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class VibeCutJobManager;

struct VibeCutExtractorProviderContext
{
    VibeCutJobManager *jobs = nullptr;
    /** Canonical evidence sink. Provider adapters submit only extractor-owned
     * records through this callback; the evidence ledger validates source,
     * fingerprint, extractor identity/version, ranges and confidence before
     * atomically replacing that extractor slice.
     */
    std::function<bool(const QString &sourceId,
                       const QString &sourceFingerprint,
                       const QString &extractorId,
                       const QString &extractorVersion,
                       const QList<VibeCutMediaEvidenceRecord> &records,
                       QString *error)> persistEvidence;
};

/** Provider-neutral capability boundary for model-backed media extractors.
 *
 * Deterministic built-in extractors (FFmpeg source/silence/loudness/shot/QA)
 * remain native VibeCut tools. OCR, diarization, object/subject models and
 * embeddings can register here without coupling the planner or evidence
 * ledger to one vendor/runtime. Provider results enter through the validated
 * evidence sink; the planner never gets a generic evidence-write capability.
 */
class VibeCutExtractorProvider
{
public:
    virtual ~VibeCutExtractorProvider() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList capabilities() const = 0;
    virtual bool configured(QString *error = nullptr) const = 0;

    /** Start a provider operation. Input is provider-neutral at the registry
     * boundary: capability, source/bin identity and optional parameters.
     * Implementations return {ok, job_id/...}; long work uses context.jobs and
     * completed evidence must flow through context.persistEvidence.
     */
    virtual QJsonObject start(const QString &capability, const QJsonObject &input,
                              const VibeCutExtractorProviderContext &context,
                              QString *error = nullptr) = 0;
};

class VibeCutExtractorProviderRegistry
{
public:
    typedef std::function<std::unique_ptr<VibeCutExtractorProvider>()> Factory;

    bool registerProvider(const QString &id, const Factory &factory, QString *error = nullptr);
    QStringList providerIds() const;
    std::unique_ptr<VibeCutExtractorProvider> create(const QString &id, QString *error = nullptr) const;
    QStringList providerIdsForCapability(const QString &capability) const;

    static VibeCutExtractorProviderRegistry &global();

private:
    QHash<QString, Factory> m_factories;
};
