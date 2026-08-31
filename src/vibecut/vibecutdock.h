/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QMetaObject>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QUrl;
class VibeCutAgent;
class VibeCutTools;

/** The VibeCut assistant dock: chat, evidence, trust policy and plan review. */
class VibeCutDock : public QWidget
{
    Q_OBJECT
public:
    explicit VibeCutDock(QWidget *parent = nullptr);

private Q_SLOTS:
    void submit();
    void onSuggestionClicked(const QUrl &url);
    void newChat();

private:
    void appendWelcome();
    void runNoiseSuggestion();
    void sendPrompt(const QString &text);
    void setBusyUi(bool busy);
    void setPlanReviewVisible(bool visible);
    void appendLine(const QString &text, const QString &cssColor = QString());
    void appendNextStepSuggestions();
    void cancelPendingSelection();
    QString describeTool(const QString &name, const QString &argsJson) const;
    QString describeToolResult(const QString &name, const QString &resultJson) const;
    void offerSpeechSetup();

    QTextBrowser *m_transcript;
    QLabel *m_status;
    QProgressBar *m_progress;
    QComboBox *m_trustMode;
    QPushButton *m_newChat;
    QPushButton *m_approvePlan;
    QPushButton *m_cancelPlan;
    QLineEdit *m_input;
    QPushButton *m_send;

    VibeCutTools *m_tools;
    VibeCutAgent *m_agent;
    bool m_streamStarted = false;
    QString m_pendingPlanSummary;

    QString m_pendingPrompt;
    bool m_awaitingSelection = false;
    QMetaObject::Connection m_selectionConn;
};
