/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutruntimeprotocoladapter.h"

#include "vibecutjobmanager.h"
#include "vibecutmediaevidence.h"
#include "vibecutplangate.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <cmath>

namespace {
constexpr qint64 MaxExactJsonInteger = 9007199254740991LL;

QString trustModeName(VibeCutTrustMode mode)
{
    switch (mode) {
    case VibeCutTrustMode::Off: return QStringLiteral("off");
    case VibeCutTrustMode::Auto: return QStringLiteral("auto");
    case VibeCutTrustMode::Turbo: return QStringLiteral("turbo");
    }
    return QStringLiteral("off");
}

QString jobStateName(VibeCutJobState state)
{
    switch (state) {
    case VibeCutJobState::Queued: return QStringLiteral("queued");
    case VibeCutJobState::Running: return QStringLiteral("running");
    case VibeCutJobState::CancelRequested: return QStringLiteral("cancel_requested");
    case VibeCutJobState::Succeeded: return QStringLiteral("succeeded");
    case VibeCutJobState::Failed: return QStringLiteral("failed");
    case VibeCutJobState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

QJsonObject jobJson(const VibeCutJob &job)
{
    QJsonObject object{{QStringLiteral("id"), job.id},
                       {QStringLiteral("kind"), job.kind},
                       {QStringLiteral("label"), job.label},
                       {QStringLiteral("state"), jobStateName(job.state)},
                       {QStringLiteral("progress"), job.progress},
                       {QStringLiteral("message"), job.message},
                       {QStringLiteral("cancelable"), job.cancelable}};
    if (!job.result.isEmpty()) object.insert(QStringLiteral("result"), job.result);
    return object;
}

bool exactInteger(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 &result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) return false;
    result = static_cast<qint64>(number);
    return true;
}

bool exactRevision(const QJsonObject &payload, const QString &key, quint64 &revision)
{
    qint64 value = -1;
    if (!exactInteger(payload.value(key), 0, MaxExactJsonInteger, value)) return false;
    revision = static_cast<quint64>(value);
    return true;
}

bool sameEvidenceSlice(const VibeCutMediaEvidenceRecord &a, const VibeCutMediaEvidenceRecord &b)
{
    return a.sourceId == b.sourceId && a.sourceFingerprint == b.sourceFingerprint &&
           a.extractorId == b.extractorId && a.extractorVersion == b.extractorVersion;
}
} // namespace

VibeCutRuntimeProtocolAdapter::VibeCutRuntimeProtocolAdapter(VibeCutToolSurface *surface,
                                                             RevisionProvider revisionProvider,
                                                             QObject *parent)
    : QObject(parent)
    , m_surface(surface)
    , m_revisionProvider(std::move(revisionProvider))
{
    if (!m_revisionProvider) {
        m_revisionProvider = [surface]() { return surface ? surface->projectRevision() : 0; };
    }
    if (m_surface && m_surface->baseTools() && m_surface->baseTools()->jobManager()) {
        connect(m_surface->baseTools()->jobManager(), &VibeCutJobManager::jobChanged,
                this, &VibeCutRuntimeProtocolAdapter::onJobChanged);
    }
}

quint64 VibeCutRuntimeProtocolAdapter::currentRevision() const
{
    return m_revisionProvider ? m_revisionProvider() : 0;
}

QJsonObject VibeCutRuntimeProtocolAdapter::helloEnvelope(const QString &messageId, VibeCutTrustMode mode) const
{
    QJsonObject payload = m_surface ? m_surface->runtimeContractSnapshot() : QJsonObject();
    payload.remove(QStringLiteral("protocol_version"));
    payload.insert(QStringLiteral("protocol_versions"), QJsonArray{1});
    payload.insert(QStringLiteral("project_revision"), static_cast<qint64>(currentRevision()));
    payload.insert(QStringLiteral("trust_mode"), trustModeName(mode));
    if (!payload.contains(QStringLiteral("editor_id"))) payload.insert(QStringLiteral("editor_id"), QStringLiteral("kdenlive"));
    if (!payload.contains(QStringLiteral("adapter_id"))) payload.insert(QStringLiteral("adapter_id"), QStringLiteral("halthinks-vibecut-adapter"));
    if (!payload.contains(QStringLiteral("tools"))) payload.insert(QStringLiteral("tools"), QJsonArray());
    payload.remove(QStringLiteral("tool_count"));
    return QJsonObject{{QStringLiteral("v"), 1},
                       {QStringLiteral("id"), messageId.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : messageId.trimmed()},
                       {QStringLiteral("kind"), QStringLiteral("event")},
                       {QStringLiteral("type"), QStringLiteral("hello")},
                       {QStringLiteral("payload"), payload}};
}

QJsonObject VibeCutRuntimeProtocolAdapter::responseEnvelope(const QJsonObject &request, const QJsonObject &payload) const
{
    return QJsonObject{{QStringLiteral("v"), 1},
                       {QStringLiteral("id"), request.value(QStringLiteral("id")).toString()},
                       {QStringLiteral("kind"), QStringLiteral("response")},
                       {QStringLiteral("type"), request.value(QStringLiteral("type")).toString()},
                       {QStringLiteral("payload"), payload}};
}

QJsonObject VibeCutRuntimeProtocolAdapter::errorEnvelope(const QJsonObject &request, const QString &code,
                                                         const QString &message, bool retryable,
                                                         const QJsonObject &details) const
{
    const QString requestId = request.value(QStringLiteral("id")).toString().trimmed();
    return QJsonObject{{QStringLiteral("v"), 1},
                       {QStringLiteral("id"), requestId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : requestId},
                       {QStringLiteral("kind"), request.value(QStringLiteral("kind")).toString() == QLatin1String("request")
                                                    ? QStringLiteral("response") : QStringLiteral("event")},
                       {QStringLiteral("type"), QStringLiteral("error")},
                       {QStringLiteral("payload"), QJsonObject{{QStringLiteral("code"), code},
                                                               {QStringLiteral("message"), message},
                                                               {QStringLiteral("retryable"), retryable},
                                                               {QStringLiteral("details"), details}}}};
}

QJsonObject VibeCutRuntimeProtocolAdapter::eventEnvelope(const QString &type, const QJsonObject &payload) const
{
    return QJsonObject{{QStringLiteral("v"), 1},
                       {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                       {QStringLiteral("kind"), QStringLiteral("event")},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("payload"), payload}};
}

void VibeCutRuntimeProtocolAdapter::clearAuthorization()
{
    m_authorizationId.clear();
    m_approvedOperationIds.clear();
    m_completedOperationIds.clear();
    m_authorizedPolicies.clear();
    m_expectedRevision = currentRevision();
    m_authorizedMode = VibeCutTrustMode::Off;
    m_waitingJobs.clear();
}

void VibeCutRuntimeProtocolAdapter::clearPlan()
{
    clearAuthorization();
    m_plan = VibeCutEditPlan();
    m_hasPlan = false;
    m_executionOrder.clear();
}

const VibeCutPlanOperation *VibeCutRuntimeProtocolAdapter::operationById(const QString &id) const
{
    for (const VibeCutPlanOperation &operation : m_plan.operations) {
        if (operation.id == id) return &operation;
    }
    return nullptr;
}

bool VibeCutRuntimeProtocolAdapter::operationDependenciesComplete(const VibeCutPlanOperation &operation, QString *error) const
{
    if (error) error->clear();
    for (const QString &dependency : operation.dependsOn) {
        if (!m_completedOperationIds.contains(dependency)) {
            if (error) *error = QStringLiteral("Operation %1 depends on incomplete operation %2.").arg(operation.id, dependency);
            return false;
        }
    }
    return true;
}

bool VibeCutRuntimeProtocolAdapter::remainingOperations() const
{
    for (const QString &operationId : m_executionOrder) {
        if (!m_completedOperationIds.contains(operationId)) return true;
    }
    return false;
}

QString VibeCutRuntimeProtocolAdapter::nextOperationId() const
{
    for (const QString &operationId : m_executionOrder) {
        if (!m_completedOperationIds.contains(operationId)) return operationId;
    }
    return QString();
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleRequest(const QJsonObject &envelope)
{
    if (envelope.value(QStringLiteral("v")).toInt(-1) != 1) {
        return errorEnvelope(envelope, QStringLiteral("unsupported_version"), QStringLiteral("Runtime protocol version must be 1."));
    }
    if (!envelope.value(QStringLiteral("id")).isString() || envelope.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
        return errorEnvelope(envelope, QStringLiteral("invalid_envelope"), QStringLiteral("Protocol request requires a non-empty id."));
    }
    if (envelope.value(QStringLiteral("kind")).toString() != QLatin1String("request") ||
        !envelope.value(QStringLiteral("type")).isString() ||
        !envelope.value(QStringLiteral("payload")).isObject()) {
        return errorEnvelope(envelope, QStringLiteral("invalid_envelope"), QStringLiteral("Protocol input must be a v1 request with string type and object payload."));
    }
    if (!m_surface) return errorEnvelope(envelope, QStringLiteral("adapter_unavailable"), QStringLiteral("VibeCutToolSurface is unavailable."));

    const QString type = envelope.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("inspect")) return handleInspect(envelope);
    if (type == QLatin1String("propose_plan")) return handleProposePlan(envelope);
    if (type == QLatin1String("invoke")) return handleInvoke(envelope);
    if (type == QLatin1String("verify")) return handleVerify(envelope);
    if (type == QLatin1String("complete_plan")) return handleCompletePlan(envelope);
    if (type == QLatin1String("abort_plan")) return handleAbortPlan(envelope);
    if (type == QLatin1String("evidence_put")) return handleEvidencePut(envelope);
    if (type == QLatin1String("evidence_get")) return handleEvidenceGet(envelope);
    return errorEnvelope(envelope, QStringLiteral("unsupported_request"),
                         QStringLiteral("Adapter does not accept runtime request type '%1'.").arg(type));
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleInspect(const QJsonObject &request)
{
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    const QString tool = payload.value(QStringLiteral("operation")).toString().trimmed();
    if (tool.isEmpty() || !payload.value(QStringLiteral("input")).isObject()) {
        return errorEnvelope(request, QStringLiteral("invalid_inspect"), QStringLiteral("inspect requires operation and object input."));
    }
    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    const auto policy = policies.constFind(tool);
    if (policy == policies.constEnd() || !policy.value().enabled || policy.value().risk != VibeCutToolRisk::ReadOnly) {
        return errorEnvelope(request, QStringLiteral("inspect_not_read_only"), QStringLiteral("inspect may invoke advertised read-only tools only."));
    }
    const quint64 before = currentRevision();
    const QJsonObject result = m_surface->invoke(tool, payload.value(QStringLiteral("input")).toObject());
    const quint64 after = currentRevision();
    if (before != after) {
        return errorEnvelope(request, QStringLiteral("inspection_revision_changed"),
                             QStringLiteral("Project revision changed while read-only inspection was running; refusing a mixed-state inspection result."));
    }
    return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), result.value(QStringLiteral("ok")).toBool(false)},
                                                  {QStringLiteral("operation"), tool},
                                                  {QStringLiteral("project_revision"), static_cast<qint64>(after)},
                                                  {QStringLiteral("result"), result}});
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleProposePlan(const QJsonObject &request)
{
    if (m_hasPlan) return errorEnvelope(request, QStringLiteral("plan_busy"), QStringLiteral("Another protocol plan is already pending or authorized."));
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    quint64 rawRevision = 0;
    if (!exactRevision(payload, QStringLiteral("base_revision"), rawRevision) ||
        !payload.value(QStringLiteral("id")).isString() || !payload.value(QStringLiteral("objective")).isString() ||
        !payload.value(QStringLiteral("operations")).isArray()) {
        return errorEnvelope(request, QStringLiteral("invalid_plan"), QStringLiteral("propose_plan payload does not satisfy the v1 plan scalar/container contract."));
    }

    const VibeCutEditPlan plan = VibeCutEditPlan::fromJson(payload);
    const VibeCutPlanValidation validation = plan.validate();
    if (!validation.ok) {
        return errorEnvelope(request, QStringLiteral("invalid_plan"), validation.errors.join(QStringLiteral("; ")));
    }
    const quint64 revision = currentRevision();
    if (plan.baseRevision != revision) {
        return errorEnvelope(request, QStringLiteral("stale_revision"),
                             QStringLiteral("Plan base_revision %1 does not match current revision %2.").arg(plan.baseRevision).arg(revision));
    }

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    bool hasEffect = false;
    for (const VibeCutPlanOperation &operation : plan.operations) {
        const auto policy = policies.constFind(operation.toolName);
        if (policy == policies.constEnd() || !policy.value().enabled) {
            return errorEnvelope(request, QStringLiteral("unknown_or_denied_tool"),
                                 QStringLiteral("Plan references unavailable tool '%1'.").arg(operation.toolName));
        }
        if (policy.value().risk != VibeCutToolRisk::ReadOnly) hasEffect = true;
    }
    if (!hasEffect) {
        return errorEnvelope(request, QStringLiteral("read_only_plan"), QStringLiteral("Read-only inspection does not require a governed EditPlan."));
    }

    const VibeCutPlanGateResult ordering = VibeCutPlanGate::assess(plan, revision, policies, VibeCutTrustMode::Turbo, true);
    if (!ordering.ready()) {
        return errorEnvelope(request, QStringLiteral("invalid_plan"), ordering.errors.join(QStringLiteral("; ")));
    }

    m_plan = plan;
    m_hasPlan = true;
    m_executionOrder = ordering.executionOrder;
    clearAuthorization();

    return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), true},
                                                  {QStringLiteral("plan_id"), m_plan.id},
                                                  {QStringLiteral("base_revision"), static_cast<qint64>(m_plan.baseRevision)},
                                                  {QStringLiteral("requires_confirmation_off"), m_plan.requiresConfirmation(policies, VibeCutTrustMode::Off)},
                                                  {QStringLiteral("requires_confirmation_auto"), m_plan.requiresConfirmation(policies, VibeCutTrustMode::Auto)},
                                                  {QStringLiteral("requires_confirmation_turbo"), m_plan.requiresConfirmation(policies, VibeCutTrustMode::Turbo)}});
}

QJsonObject VibeCutRuntimeProtocolAdapter::authorizePending(VibeCutTrustMode mode, bool humanApproved,
                                                            bool humanDecisionPresent, const QString &messageId)
{
    const QString id = messageId.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : messageId.trimmed();
    const QString kind = messageId.trimmed().isEmpty() ? QStringLiteral("event") : QStringLiteral("response");
    const QJsonObject request{{QStringLiteral("v"), 1}, {QStringLiteral("id"), id},
                              {QStringLiteral("kind"), kind == QLatin1String("response") ? QStringLiteral("request") : QStringLiteral("event")},
                              {QStringLiteral("type"), QStringLiteral("authorize")}, {QStringLiteral("payload"), QJsonObject()}};
    if (!m_hasPlan || !m_surface) {
        QJsonObject error = errorEnvelope(request, QStringLiteral("no_pending_plan"), QStringLiteral("There is no protocol plan awaiting authorization."));
        error.insert(QStringLiteral("kind"), kind);
        return error;
    }

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    const bool confirmationRequired = m_plan.requiresConfirmation(policies, mode);
    if (confirmationRequired && !humanDecisionPresent) {
        QJsonObject error = errorEnvelope(request, QStringLiteral("confirmation_required"),
                                          QStringLiteral("Current plan requires an explicit human authorization decision in this trust mode."));
        error.insert(QStringLiteral("kind"), kind);
        return error;
    }
    if (humanDecisionPresent && !humanApproved) {
        const QString planId = m_plan.id;
        clearPlan();
        return QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("id"), id}, {QStringLiteral("kind"), kind},
                           {QStringLiteral("type"), QStringLiteral("authorize")},
                           {QStringLiteral("payload"), QJsonObject{{QStringLiteral("plan_id"), planId},
                                                                   {QStringLiteral("decision"), QStringLiteral("rejected")},
                                                                   {QStringLiteral("trust_mode"), trustModeName(mode)},
                                                                   {QStringLiteral("reason"), QStringLiteral("Human rejected the proposed plan.")}}}};
    }

    const bool approvedForGate = humanDecisionPresent && humanApproved;
    const quint64 revision = currentRevision();
    const VibeCutPlanGateResult gate = VibeCutPlanGate::assess(m_plan, revision, policies, mode, approvedForGate);
    if (!gate.ready()) {
        QJsonObject error = errorEnvelope(request,
                                          gate.status == VibeCutPlanGateStatus::StalePlan ? QStringLiteral("stale_revision") : QStringLiteral("authorization_failed"),
                                          gate.errors.join(QStringLiteral("; ")));
        error.insert(QStringLiteral("kind"), kind);
        if (gate.status == VibeCutPlanGateStatus::StalePlan || gate.status == VibeCutPlanGateStatus::UnknownTool ||
            gate.status == VibeCutPlanGateStatus::ToolDenied) clearPlan();
        return error;
    }

    clearAuthorization();
    m_executionOrder = gate.executionOrder;
    m_authorizationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_expectedRevision = revision;
    m_authorizedMode = mode;
    for (const QString &operationId : m_executionOrder) {
        m_approvedOperationIds.insert(operationId);
        const VibeCutPlanOperation *operation = operationById(operationId);
        if (operation) m_authorizedPolicies.insert(operationId, policies.value(operation->toolName));
    }

    QJsonArray approvedIds;
    for (const QString &operationId : m_executionOrder) approvedIds.append(operationId);
    return QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("id"), id}, {QStringLiteral("kind"), kind},
                       {QStringLiteral("type"), QStringLiteral("authorize")},
                       {QStringLiteral("payload"), QJsonObject{{QStringLiteral("plan_id"), m_plan.id},
                                                               {QStringLiteral("decision"), QStringLiteral("approved")},
                                                               {QStringLiteral("trust_mode"), trustModeName(mode)},
                                                               {QStringLiteral("authorization_id"), m_authorizationId},
                                                               {QStringLiteral("expected_revision"), static_cast<qint64>(m_expectedRevision)},
                                                               {QStringLiteral("approved_operation_ids"), approvedIds}}}};
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleInvoke(const QJsonObject &request)
{
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    if (payload.contains(QStringLiteral("tool")) || payload.contains(QStringLiteral("input"))) {
        return errorEnvelope(request, QStringLiteral("plan_substitution_attempt"),
                             QStringLiteral("invoke may not supply tool or input; adapter resolves the exact approved operation."));
    }
    const QString planId = payload.value(QStringLiteral("plan_id")).toString().trimmed();
    const QString authorization = payload.value(QStringLiteral("authorization_id")).toString().trimmed();
    const QString operationId = payload.value(QStringLiteral("operation_id")).toString().trimmed();
    quint64 requestedRevision = 0;
    if (!exactRevision(payload, QStringLiteral("expected_revision"), requestedRevision) || planId.isEmpty() || authorization.isEmpty() || operationId.isEmpty()) {
        return errorEnvelope(request, QStringLiteral("invalid_invoke"), QStringLiteral("invoke requires plan_id, authorization_id, operation_id and exact expected_revision."));
    }
    if (!m_hasPlan || planId != m_plan.id || authorization != m_authorizationId || m_authorizationId.isEmpty()) {
        return errorEnvelope(request, QStringLiteral("invalid_authorization"), QStringLiteral("invoke does not match the active approved plan authorization."));
    }
    if (!m_waitingJobs.isEmpty()) {
        return errorEnvelope(request, QStringLiteral("job_pending"), QStringLiteral("A protocol plan operation is still waiting on a background job."));
    }
    if (requestedRevision != m_expectedRevision || currentRevision() != m_expectedRevision) {
        clearAuthorization();
        return errorEnvelope(request, QStringLiteral("stale_revision"), QStringLiteral("expected_revision no longer matches adapter project revision."));
    }
    if (!m_approvedOperationIds.contains(operationId) || m_completedOperationIds.contains(operationId)) {
        return errorEnvelope(request, QStringLiteral("operation_not_approved"), QStringLiteral("Operation is not approved or has already completed."));
    }
    if (nextOperationId() != operationId) {
        return errorEnvelope(request, QStringLiteral("operation_out_of_order"), QStringLiteral("Operation is not the next approved dependency-ordered step."));
    }
    const VibeCutPlanOperation *operation = operationById(operationId);
    if (!operation) return errorEnvelope(request, QStringLiteral("missing_operation"), QStringLiteral("Stored approved operation is missing."));
    QString dependencyError;
    if (!operationDependenciesComplete(*operation, &dependencyError)) {
        return errorEnvelope(request, QStringLiteral("dependency_incomplete"), dependencyError);
    }

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    const auto currentPolicy = policies.constFind(operation->toolName);
    const auto approvedPolicy = m_authorizedPolicies.constFind(operationId);
    if (currentPolicy == policies.constEnd() || approvedPolicy == m_authorizedPolicies.constEnd() ||
        !currentPolicy.value().enabled || currentPolicy.value().toJson() != approvedPolicy.value().toJson()) {
        clearAuthorization();
        return errorEnvelope(request, QStringLiteral("policy_changed"),
                             QStringLiteral("Effective tool policy changed after authorization; plan must be re-proposed/re-authorized."));
    }

    const quint64 before = currentRevision();
    const QJsonObject result = m_surface->invoke(operation->toolName, operation->input);
    const quint64 after = currentRevision();
    if (!result.value(QStringLiteral("ok")).toBool(false)) {
        clearAuthorization();
        return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), false},
                                                      {QStringLiteral("plan_id"), m_plan.id},
                                                      {QStringLiteral("operation_id"), operationId},
                                                      {QStringLiteral("tool"), operation->toolName},
                                                      {QStringLiteral("revision_before"), static_cast<qint64>(before)},
                                                      {QStringLiteral("revision_after"), static_cast<qint64>(after)},
                                                      {QStringLiteral("result"), result}});
    }

    m_expectedRevision = after;
    QJsonObject response{{QStringLiteral("ok"), true},
                         {QStringLiteral("plan_id"), m_plan.id},
                         {QStringLiteral("operation_id"), operationId},
                         {QStringLiteral("tool"), operation->toolName},
                         {QStringLiteral("revision_before"), static_cast<qint64>(before)},
                         {QStringLiteral("revision_after"), static_cast<qint64>(after)},
                         {QStringLiteral("result"), result}};

    if (result.value(QStringLiteral("started")).toBool(false)) {
        const QString jobId = result.value(QStringLiteral("job_id")).toString().trimmed();
        if (jobId.isEmpty()) {
            clearAuthorization();
            return errorEnvelope(request, QStringLiteral("untrackable_async_operation"),
                                 QStringLiteral("Approved operation started asynchronously without a job_id."));
        }
        WaitingJob waiting;
        waiting.operationId = operationId;
        waiting.policy = currentPolicy.value();
        waiting.revisionAtStart = after;
        m_waitingJobs.insert(jobId, waiting);
        response.insert(QStringLiteral("started"), true);
        response.insert(QStringLiteral("job_id"), jobId);
    } else {
        m_completedOperationIds.insert(operationId);
        response.insert(QStringLiteral("completed"), true);
    }
    response.insert(QStringLiteral("plan_complete_ready"), !remainingOperations() && m_waitingJobs.isEmpty());
    return responseEnvelope(request, response);
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleVerify(const QJsonObject &request)
{
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    const QString planId = payload.value(QStringLiteral("plan_id")).toString().trimmed();
    const QString authorization = payload.value(QStringLiteral("authorization_id")).toString().trimmed();
    const QString operationId = payload.value(QStringLiteral("operation_id")).toString().trimmed();
    const QString inspection = payload.value(QStringLiteral("inspection")).toString().trimmed();
    quint64 requestedRevision = 0;
    if (!exactRevision(payload, QStringLiteral("expected_revision"), requestedRevision) || planId != m_plan.id ||
        authorization != m_authorizationId || authorization.isEmpty() || operationId.isEmpty() || inspection.isEmpty()) {
        return errorEnvelope(request, QStringLiteral("invalid_verify"), QStringLiteral("verify does not match the active authorized plan/revision."));
    }
    if (!m_completedOperationIds.contains(operationId)) {
        return errorEnvelope(request, QStringLiteral("operation_incomplete"), QStringLiteral("verify requires a terminal-successful operation."));
    }
    if (requestedRevision != m_expectedRevision || currentRevision() != m_expectedRevision) {
        clearAuthorization();
        return errorEnvelope(request, QStringLiteral("stale_revision"), QStringLiteral("Project changed before verification."));
    }
    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    const auto policy = policies.constFind(inspection);
    if (policy == policies.constEnd() || !policy.value().enabled || policy.value().risk != VibeCutToolRisk::ReadOnly) {
        return errorEnvelope(request, QStringLiteral("invalid_verification_inspection"), QStringLiteral("verify inspection must be an advertised read-only tool."));
    }
    const QJsonObject inspectionInput = payload.value(QStringLiteral("inspection_input")).isObject()
                                                ? payload.value(QStringLiteral("inspection_input")).toObject() : QJsonObject();
    const quint64 before = currentRevision();
    const QJsonObject result = m_surface->invoke(inspection, inspectionInput);
    const quint64 after = currentRevision();
    if (before != after) {
        clearAuthorization();
        return errorEnvelope(request, QStringLiteral("verification_revision_changed"), QStringLiteral("Project revision changed during verification inspection."));
    }
    return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), result.value(QStringLiteral("ok")).toBool(false)},
                                                  {QStringLiteral("plan_id"), m_plan.id},
                                                  {QStringLiteral("operation_id"), operationId},
                                                  {QStringLiteral("project_revision"), static_cast<qint64>(after)},
                                                  {QStringLiteral("inspection"), inspection},
                                                  {QStringLiteral("expected_postconditions"), payload.value(QStringLiteral("expected_postconditions")).toArray()},
                                                  {QStringLiteral("inspection_result"), result},
                                                  {QStringLiteral("verification_semantics"), QStringLiteral("adapter_inspection_evidence_not_freeform_postcondition_interpretation")}});
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleCompletePlan(const QJsonObject &request)
{
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    const QString planId = payload.value(QStringLiteral("plan_id")).toString().trimmed();
    const QString authorization = payload.value(QStringLiteral("authorization_id")).toString().trimmed();
    quint64 requestedRevision = 0;
    if (!exactRevision(payload, QStringLiteral("expected_revision"), requestedRevision) || !m_hasPlan ||
        planId != m_plan.id || authorization != m_authorizationId || authorization.isEmpty()) {
        return errorEnvelope(request, QStringLiteral("invalid_completion"), QStringLiteral("complete_plan does not match the active authorization."));
    }
    if (!m_waitingJobs.isEmpty() || remainingOperations()) {
        return errorEnvelope(request, QStringLiteral("plan_incomplete"), QStringLiteral("complete_plan requires every approved operation to be terminal-successful."));
    }
    if (requestedRevision != m_expectedRevision || currentRevision() != m_expectedRevision) {
        clearAuthorization();
        return errorEnvelope(request, QStringLiteral("stale_revision"), QStringLiteral("Project changed before plan completion."));
    }
    const QString completedPlanId = m_plan.id;
    const quint64 revision = m_expectedRevision;
    clearPlan();
    return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), true},
                                                  {QStringLiteral("plan_id"), completedPlanId},
                                                  {QStringLiteral("completed"), true},
                                                  {QStringLiteral("project_revision"), static_cast<qint64>(revision)},
                                                  {QStringLiteral("checkpoint_rollback_parity"), false},
                                                  {QStringLiteral("note"), QStringLiteral("Step-2 protocol seam complete; plan-wide undo checkpoint/rollback parity remains Step 4.")}});
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleAbortPlan(const QJsonObject &request)
{
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    const QString planId = payload.value(QStringLiteral("plan_id")).toString().trimmed();
    const QString authorization = payload.value(QStringLiteral("authorization_id")).toString().trimmed();
    const QString reason = payload.value(QStringLiteral("reason")).toString().trimmed();
    if (!m_hasPlan || planId != m_plan.id || reason.isEmpty()) {
        return errorEnvelope(request, QStringLiteral("invalid_abort"), QStringLiteral("abort_plan requires the active plan_id and a reason."));
    }
    if (!authorization.isEmpty() && authorization != m_authorizationId) {
        return errorEnvelope(request, QStringLiteral("invalid_authorization"), QStringLiteral("abort_plan authorization_id does not match the active authorization."));
    }
    int cancelRequests = 0;
    if (m_surface && m_surface->baseTools() && m_surface->baseTools()->jobManager()) {
        VibeCutJobManager *jobs = m_surface->baseTools()->jobManager();
        for (auto it = m_waitingJobs.constBegin(); it != m_waitingJobs.constEnd(); ++it) {
            VibeCutJob job;
            if (jobs->job(it.key(), job) && job.cancelable && !job.terminal() && jobs->requestCancel(it.key())) ++cancelRequests;
        }
    }
    const QString abortedPlanId = m_plan.id;
    clearPlan();
    return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), true},
                                                  {QStringLiteral("plan_id"), abortedPlanId},
                                                  {QStringLiteral("aborted"), true},
                                                  {QStringLiteral("reason"), reason},
                                                  {QStringLiteral("job_cancel_requests"), cancelRequests},
                                                  {QStringLiteral("rollback_performed"), false},
                                                  {QStringLiteral("note"), QStringLiteral("Step-2 abort invalidates authority and requests cancellable jobs; plan-wide rollback parity remains Step 4.")}});
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleEvidencePut(const QJsonObject &request)
{
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    if (!payload.value(QStringLiteral("records")).isArray()) {
        return errorEnvelope(request, QStringLiteral("invalid_evidence"), QStringLiteral("evidence_put requires a records array."));
    }
    const QJsonArray raw = payload.value(QStringLiteral("records")).toArray();
    if (raw.isEmpty() || raw.size() > VibeCutMediaEvidence::MaxRecords) {
        return errorEnvelope(request, QStringLiteral("invalid_evidence"), QStringLiteral("evidence_put requires 1..%1 records.").arg(VibeCutMediaEvidence::MaxRecords));
    }
    QList<VibeCutMediaEvidenceRecord> records;
    for (const QJsonValue &value : raw) {
        if (!value.isObject()) return errorEnvelope(request, QStringLiteral("invalid_evidence"), QStringLiteral("Every evidence record must be an object."));
        VibeCutMediaEvidenceRecord record;
        QString error;
        if (!VibeCutMediaEvidenceRecord::fromJson(value.toObject(), record, &error)) {
            return errorEnvelope(request, QStringLiteral("invalid_evidence"), error);
        }
        if (!records.isEmpty() && !sameEvidenceSlice(records.first(), record)) {
            return errorEnvelope(request, QStringLiteral("mixed_evidence_slice"),
                                 QStringLiteral("One v1 evidence_put may replace only one exact source/fingerprint/extractor/version slice."));
        }
        records.append(record);
    }
    const VibeCutMediaEvidenceRecord first = records.first();
    QString persistError;
    if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(first.sourceId, first.sourceFingerprint,
                                                              first.extractorId, first.extractorVersion,
                                                              records, &persistError)) {
        return errorEnvelope(request, QStringLiteral("evidence_persist_failed"), persistError);
    }
    return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), true},
                                                  {QStringLiteral("record_count"), records.size()},
                                                  {QStringLiteral("source_id"), first.sourceId},
                                                  {QStringLiteral("source_fingerprint"), first.sourceFingerprint},
                                                  {QStringLiteral("extractor_id"), first.extractorId},
                                                  {QStringLiteral("extractor_version"), first.extractorVersion},
                                                  {QStringLiteral("authority"), QStringLiteral("evidence_persistence_not_project_truth")}});
}

QJsonObject VibeCutRuntimeProtocolAdapter::handleEvidenceGet(const QJsonObject &request)
{
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    qint64 limitValue = 1000;
    if (payload.contains(QStringLiteral("limit")) && !exactInteger(payload.value(QStringLiteral("limit")), 1, 10000, limitValue)) {
        return errorEnvelope(request, QStringLiteral("invalid_evidence_filter"), QStringLiteral("evidence_get limit must be an integer 1..10000."));
    }
    qint64 startFilter = -1;
    qint64 endFilter = -1;
    if (payload.contains(QStringLiteral("start_frame")) && !exactInteger(payload.value(QStringLiteral("start_frame")), -1, 2147483647, startFilter)) {
        return errorEnvelope(request, QStringLiteral("invalid_evidence_filter"), QStringLiteral("start_frame must be an integer >= -1."));
    }
    if (payload.contains(QStringLiteral("end_frame")) && !exactInteger(payload.value(QStringLiteral("end_frame")), -1, 2147483647, endFilter)) {
        return errorEnvelope(request, QStringLiteral("invalid_evidence_filter"), QStringLiteral("end_frame must be an integer >= -1."));
    }
    const QString sourceId = payload.value(QStringLiteral("source_id")).toString().trimmed();
    const QString fingerprint = payload.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    const QString extractorId = payload.value(QStringLiteral("extractor_id")).toString().trimmed();
    const QString extractorVersion = payload.value(QStringLiteral("extractor_version")).toString().trimmed();
    const QString kind = payload.value(QStringLiteral("kind")).toString().trimmed();

    QString loadError;
    const QJsonArray all = VibeCutMediaEvidence::loadCurrent(&loadError);
    if (!loadError.isEmpty()) return errorEnvelope(request, QStringLiteral("evidence_load_failed"), loadError);
    QJsonArray matches;
    for (const QJsonValue &value : all) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        if (!sourceId.isEmpty() && object.value(QStringLiteral("source_id")).toString() != sourceId) continue;
        if (!fingerprint.isEmpty() && object.value(QStringLiteral("source_fingerprint")).toString() != fingerprint) continue;
        if (!extractorId.isEmpty() && object.value(QStringLiteral("extractor_id")).toString() != extractorId) continue;
        if (!extractorVersion.isEmpty() && object.value(QStringLiteral("extractor_version")).toString() != extractorVersion) continue;
        if (!kind.isEmpty() && object.value(QStringLiteral("kind")).toString() != kind) continue;
        const int start = object.value(QStringLiteral("start_frame")).toInt(-1);
        const int end = object.value(QStringLiteral("end_frame")).toInt(-1);
        if (startFilter >= 0 && (end < 0 || end < startFilter)) continue;
        if (endFilter >= 0 && (start < 0 || start > endFilter)) continue;
        matches.append(object);
        if (matches.size() >= limitValue) break;
    }
    return responseEnvelope(request, QJsonObject{{QStringLiteral("ok"), true},
                                                  {QStringLiteral("record_count"), matches.size()},
                                                  {QStringLiteral("records"), matches},
                                                  {QStringLiteral("authority"), QStringLiteral("evidence_records_not_project_truth")}});
}

void VibeCutRuntimeProtocolAdapter::onJobChanged(const QString &jobId)
{
    if (!m_surface || !m_surface->baseTools() || !m_surface->baseTools()->jobManager()) return;
    VibeCutJob job;
    if (!m_surface->baseTools()->jobManager()->job(jobId, job)) return;
    const quint64 revision = currentRevision();
    Q_EMIT outboundEnvelope(eventEnvelope(QStringLiteral("job_update"),
                                          QJsonObject{{QStringLiteral("job"), jobJson(job)},
                                                      {QStringLiteral("project_revision"), static_cast<qint64>(revision)}}));

    auto waiting = m_waitingJobs.find(jobId);
    if (waiting == m_waitingJobs.end() || !job.terminal()) return;
    const WaitingJob wait = waiting.value();
    m_waitingJobs.erase(waiting);

    if (job.state != VibeCutJobState::Succeeded) {
        clearAuthorization();
        Q_EMIT outboundEnvelope(eventEnvelope(QStringLiteral("error"),
                                              QJsonObject{{QStringLiteral("code"), QStringLiteral("background_operation_failed")},
                                                          {QStringLiteral("message"), job.message.isEmpty() ? QStringLiteral("Background approved operation did not succeed.") : job.message},
                                                          {QStringLiteral("retryable"), false},
                                                          {QStringLiteral("details"), QJsonObject{{QStringLiteral("job_id"), jobId},
                                                                                                  {QStringLiteral("operation_id"), wait.operationId}}}}));
        return;
    }

    m_completedOperationIds.insert(wait.operationId);
    const bool externalOnly = !wait.policy.mutatesProject;
    const bool drifted = revision != m_expectedRevision;
    m_expectedRevision = revision;
    if (externalOnly && drifted && remainingOperations()) {
        clearAuthorization();
        Q_EMIT outboundEnvelope(eventEnvelope(QStringLiteral("error"),
                                              QJsonObject{{QStringLiteral("code"), QStringLiteral("stale_revision")},
                                                          {QStringLiteral("message"), QStringLiteral("Project changed while an external-only background operation was running; remaining approved operations were invalidated.")},
                                                          {QStringLiteral("retryable"), false},
                                                          {QStringLiteral("details"), QJsonObject{{QStringLiteral("job_id"), jobId}}}}));
    }
}
