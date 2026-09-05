/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutruntimestdiotransport.h"

#include <QProcess>

bool VibeCutRuntimeStdioTransport::waitUntilReady(int timeoutMs, QString *error)
{
    if (error) error->clear();
    if (!m_process) {
        if (error) *error = QStringLiteral("External runtime process object is unavailable.");
        return false;
    }
    if (timeoutMs < 1 || timeoutMs > 30000) {
        if (error) *error = QStringLiteral("External runtime readiness timeout must be in 1..30000 ms.");
        return false;
    }
    if (m_process->state() == QProcess::Running) return true;
    if (m_process->state() == QProcess::NotRunning) {
        if (error) {
            *error = m_process->errorString().trimmed().isEmpty()
                         ? QStringLiteral("External runtime process is not running.")
                         : m_process->errorString();
        }
        return false;
    }
    if (!m_process->waitForStarted(timeoutMs) || m_process->state() != QProcess::Running) {
        if (error) {
            *error = m_process->errorString().trimmed().isEmpty()
                         ? QStringLiteral("External runtime did not reach Running state before the bounded timeout.")
                         : m_process->errorString();
        }
        return false;
    }
    return true;
}
