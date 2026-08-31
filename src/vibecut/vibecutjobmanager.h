/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

enum class VibeCutJobState {
    Queued,
    Running,
    CancelRequested,
    Succeeded,
    Failed,
    Cancelled,
};

struct VibeCutJob {
    QString id;
    QString kind;
    QString label;
    VibeCutJobState state = VibeCutJobState::Queued;
    int progress = -1;
    QString message;
    bool cancelable = false;

    bool terminal() const;
};

class VibeCutJobManager : public QObject
{
    Q_OBJECT
public:
    explicit VibeCutJobManager(QObject *parent = nullptr);

    QString createJob(const QString &kind, const QString &label, bool cancelable = false);
    bool markRunning(const QString &id, const QString &message = QString());
    bool setProgress(const QString &id, int progress, const QString &message = QString());
    bool requestCancel(const QString &id);
    bool markSucceeded(const QString &id, const QString &message = QString());
    bool markFailed(const QString &id, const QString &message);
    bool markCancelled(const QString &id, const QString &message = QString());

    bool job(const QString &id, VibeCutJob &result) const;
    QVector<VibeCutJob> jobs() const;

Q_SIGNALS:
    void jobAdded(const QString &id);
    void jobChanged(const QString &id);

private:
    bool updateState(const QString &id, VibeCutJobState state, const QString &message);

    QHash<QString, VibeCutJob> m_jobs;
    QStringList m_order;
};
