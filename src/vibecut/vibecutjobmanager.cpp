/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutjobmanager.h"

#include <QJsonDocument>
#include <QUuid>

bool VibeCutJob::terminal() const
{
    return state == VibeCutJobState::Succeeded || state == VibeCutJobState::Failed || state == VibeCutJobState::Cancelled;
}

VibeCutJobManager::VibeCutJobManager(QObject *parent)
    : QObject(parent)
{
}

QString VibeCutJobManager::createJob(const QString &kind, const QString &label, bool cancelable)
{
    VibeCutJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.kind = kind;
    job.label = label;
    job.cancelable = cancelable;
    m_jobs.insert(job.id, job);
    m_order.append(job.id);
    Q_EMIT jobAdded(job.id);
    return job.id;
}

bool VibeCutJobManager::markRunning(const QString &id, const QString &message)
{
    return updateState(id, VibeCutJobState::Running, message);
}

bool VibeCutJobManager::setProgress(const QString &id, int progress, const QString &message)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end() || it->terminal()) {
        return false;
    }
    it->progress = qBound(0, progress, 100);
    if (!message.isNull()) {
        it->message = message;
    }
    Q_EMIT jobChanged(id);
    return true;
}

bool VibeCutJobManager::setResult(const QString &id, const QJsonObject &result, QString *error)
{
    if (error) error->clear();
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) {
        if (error) *error = QStringLiteral("Unknown VibeCut job: %1").arg(id);
        return false;
    }
    if (it->terminal()) {
        if (error) *error = QStringLiteral("Cannot change the result of terminal job %1.").arg(id);
        return false;
    }
    const QByteArray serialized = QJsonDocument(result).toJson(QJsonDocument::Compact);
    if (serialized.size() > MaxResultBytes) {
        if (error) *error = QStringLiteral("Job result exceeds the %1 byte limit.").arg(MaxResultBytes);
        return false;
    }
    it->result = result;
    Q_EMIT jobChanged(id);
    return true;
}

bool VibeCutJobManager::requestCancel(const QString &id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end() || it->terminal() || !it->cancelable) {
        return false;
    }
    it->state = VibeCutJobState::CancelRequested;
    Q_EMIT jobChanged(id);
    return true;
}

bool VibeCutJobManager::markSucceeded(const QString &id, const QString &message)
{
    return updateState(id, VibeCutJobState::Succeeded, message);
}

bool VibeCutJobManager::markFailed(const QString &id, const QString &message)
{
    return updateState(id, VibeCutJobState::Failed, message);
}

bool VibeCutJobManager::markCancelled(const QString &id, const QString &message)
{
    return updateState(id, VibeCutJobState::Cancelled, message);
}

bool VibeCutJobManager::job(const QString &id, VibeCutJob &result) const
{
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.constEnd()) {
        return false;
    }
    result = it.value();
    return true;
}

QVector<VibeCutJob> VibeCutJobManager::jobs() const
{
    QVector<VibeCutJob> result;
    result.reserve(m_order.size());
    for (const QString &id : m_order) {
        VibeCutJob value;
        if (job(id, value)) {
            result.append(value);
        }
    }
    return result;
}

bool VibeCutJobManager::updateState(const QString &id, VibeCutJobState state, const QString &message)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end() || it->terminal()) {
        return false;
    }
    it->state = state;
    if (!message.isNull()) {
        it->message = message;
    }
    if (state == VibeCutJobState::Succeeded) {
        it->progress = 100;
    } else if (state == VibeCutJobState::Failed || state == VibeCutJobState::Cancelled) {
        // Never expose a partial/stale payload as the result of a failed job.
        it->result = QJsonObject();
    }
    Q_EMIT jobChanged(id);
    return true;
}
