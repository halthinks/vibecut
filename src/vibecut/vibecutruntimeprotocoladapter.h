/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutcontracts.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>

class VibeCutToolSurface;

/** GPL protocol boundary between an out-of-process runtime and the native
 * VibeCutToolSurface. The adapter owns the exact proposed/approved plan and
 * resolves operation tool/input itself so an external runtime cannot substitute
 * a different operation after approval. */
class VibeCutRuntimeProtocolAdapter : public QObject
{
    Q_OBJECT
public:
    using RevisionProvider = std::function<quint64()>;

    explicit VibeCutRuntimeProtocolAdapter(VibeCutToolSurface *surface,
                                           RevisionProvider revisionProvider = RevisionProvider(),
                                           QObject *parent = nullptr);

    QJsonObject helloEnvelope(const QString &messageId, VibeCutTrustMode mode = VibeCutTrustMode::Off) const;
    QJsonObject handleRequest(const QJsonObject &envelope);

    /** Produce the adapter/human authorization response for the current stored
     * plan. When confirmation is not required by the effective policy/mode,
     * humanDecisionPresent may be false and approval is automatic. */
    QJsonObject authorizePending(VibeCutTrustMode mode, bool humanApproved,
                                 bool humanDecisionPresent = true,
                                 const QString &messageId = QString());

    bool hasPendingPlan() const { return m_hasPlan; }
    bool hasAuthorization() const { return !m_authorizationId.isEmpty(); }
    QString pendingPlanId() const { return m_hasPlan ? m_plan.id : QString(); }
    QString authorizationId() const { return m_authorizationId; }
    quint64 expectedRevision() const { return m_expectedRevision; }

Q_SIGNALS:
    /** Protocol event/error that an attached transport should send to runtime. */
    void outboundEnvelope(const QJsonObject &envelope);

private Q_SLOTS:
    void onJobChanged(const QString &jobId);

private:
    struct WaitingJob {
        QString operationId;
        VibeCutToolPolicy policy;
        quint64 revisionAtStart = 0;
    };

    quint64 currentRevision() const;
    void clearAuthorization();
    void clearPlan();
    const VibeCutPlanOperation *operationById(const QString &id) const;
    bool operationDependenciesComplete(const VibeCutPlanOperation &operation, QString *error) const;
    bool remainingOperations() const;

    QJsonObject responseEnvelope(const QJsonObject &request, const QJsonObject &payload) const;
    QJsonObject errorEnvelope(const QJsonObject &request, const QString &code,
                              const QString &message, bool retryable = false,
                              const QJsonObject &details = QJsonObject()) const;
    QJsonObject eventEnvelope(const QString &type, const QJsonObject &payload) const;

    QJsonObject handleInspect(const QJsonObject &request);
    QJsonObject handleProposePlan(const QJsonObject &request);
    QJsonObject handleInvoke(const QJsonObject &request);
    QJsonObject handleVerify(const QJsonObject &request);
    QJsonObject handleEvidencePut(const QJsonObject &request);
    QJsonObject handleEvidenceGet(const QJsonObject &request);

    VibeCutToolSurface *m_surface = nullptr;
    RevisionProvider m_revisionProvider;
    VibeCutEditPlan m_plan;
    bool m_hasPlan = false;
    QStringList m_executionOrder;
    QSet<QString> m_approvedOperationIds;
    QSet<QString> m_completedOperationIds;
    QString m_authorizationId;
    quint64 m_expectedRevision = 0;
    VibeCutTrustMode m_authorizedMode = VibeCutTrustMode::Off;
    QHash<QString, WaitingJob> m_waitingJobs;
};
