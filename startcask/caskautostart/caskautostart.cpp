/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 2006 Aaron Seigo <aseigo@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "caskautostart.h"

#include <KConfigGroup>
#include <KDesktopFile>

#include <QCoreApplication>
#include <QDir>
#include <QFile>

void CaskAutostart::copyIfNeeded()
{
    if (copyIfNeededChecked) {
        return;
    }

    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/autostart/") + name;

    if (!QFile::exists(local)) {
        const QString global = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QLatin1String("autostart/") + name);
        if (!global.isEmpty()) {
            KDesktopFile *newDf = df->copyTo(local);
            delete df;
            delete newDf; // Force sync-to-disk
            df = new KDesktopFile(QStandardPaths::GenericConfigLocation, QStringLiteral("autostart/") + name); // Recreate from disk
        }
    }

    copyIfNeededChecked = true;
}

CaskAutostart::CaskAutostart(const QString &entryName, QObject *parent)
    : QObject(parent)
{
    const bool isAbsolute = QDir::isAbsolutePath(entryName);
    if (isAbsolute) {
        name = entryName.mid(entryName.lastIndexOf(QLatin1Char('/')) + 1);
    } else {
        if (entryName.isEmpty()) {
            name = QCoreApplication::applicationName();
        } else {
            name = entryName;
        }

        if (!name.endsWith(QLatin1String(".desktop"))) {
            name.append(QLatin1String(".desktop"));
        }
    }

    const QString path = isAbsolute ? entryName : QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QLatin1String("autostart/") + name);
    if (path.isEmpty()) {
        // just a new KDesktopFile, since we have nothing to use
        df = new KDesktopFile(QStandardPaths::GenericConfigLocation, QLatin1String("autostart/") + name);
        copyIfNeededChecked = true;
    } else {
        df = new KDesktopFile(path);
    }
}

CaskAutostart::~CaskAutostart() = default;

void CaskAutostart::setAutostarts(bool autostart)
{
    bool currentAutostartState = !df->desktopGroup().readEntry("Hidden", false);
    if (currentAutostartState == autostart) {
        return;
    }

    copyIfNeeded();
    df->desktopGroup().writeEntry("Hidden", !autostart);
}

bool CaskAutostart::autostarts(const QString &environment, Conditions check) const
{
    // check if this is actually a .desktop file
    bool starts = df->desktopGroup().exists();

    // check the hidden field
    starts = starts && !df->desktopGroup().readEntry("Hidden", false);

    if (!environment.isEmpty()) {
        starts = starts && checkAllowedEnvironment(environment);
    }

    if (check & CheckCommand) {
        starts = starts && df->tryExec();
    }

    if (check & CheckCondition) {
        starts = starts && checkStartCondition();
    }

    return starts;
}

bool CaskAutostart::checkStartCondition() const
{
    return CaskAutostart::isStartConditionMet(df->desktopGroup().readEntry("X-KDE-autostart-condition"));
}

bool CaskAutostart::isStartConditionMet(const QString &condition)
{
    if (condition.isEmpty()) {
        return true;
    }

    const QStringList list = condition.split(QLatin1Char(':'));
    if (list.count() < 4) {
        return true;
    }

    if (list[0].isEmpty() || list[2].isEmpty()) {
        return true;
    }

    KConfig config(list[0], KConfig::NoGlobals);
    KConfigGroup cg(&config, list[1]);

    const bool defaultValue = (list[3].toLower() == QLatin1String("true"));
    return cg.readEntry(list[2], defaultValue);
}

bool CaskAutostart::checkAllowedEnvironment(const QString &environment) const
{
    const QStringList allowed = allowedEnvironments();
    if (!allowed.isEmpty()) {
        return allowed.contains(environment);
    }

    const QStringList excluded = excludedEnvironments();
    if (!excluded.isEmpty()) {
        return !excluded.contains(environment);
    }

    return true;
}

QString CaskAutostart::command() const
{
    return df->desktopGroup().readEntry("Exec", QString());
}

void CaskAutostart::setCommand(const QString &command)
{
    if (df->desktopGroup().readEntry("Exec", QString()) == command) {
        return;
    }

    copyIfNeeded();
    df->desktopGroup().writeEntry("Exec", command);
}

bool CaskAutostart::isServiceRegistered(const QString &entryName)
{
    const QString localDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/autostart/");
    return QFile::exists(localDir + entryName + QLatin1String(".desktop"));
}

// do not specialize the readEntry template -
// http://connect.microsoft.com/VisualStudio/feedback/ViewFeedback.aspx?FeedbackID=100911
static CaskAutostart::StartPhase readEntry(const KConfigGroup &group, const char *key, CaskAutostart::StartPhase aDefault)
{
    const QByteArray data = group.readEntry(key, QByteArray());

    if (data.isNull()) {
        return aDefault;
    }

    if (data == "0" || data == "BaseDesktop") {
        return CaskAutostart::BaseDesktop;
    } else if (data == "1" || data == "DesktopServices") {
        return CaskAutostart::DesktopServices;
    } else if (data == "2" || data == "Applications") {
        return CaskAutostart::Applications;
    }

    return aDefault;
}

CaskAutostart::StartPhase CaskAutostart::startPhase() const
{
    return readEntry(df->desktopGroup(), "X-KDE-autostart-phase", Applications);
}

QStringList CaskAutostart::allowedEnvironments() const
{
    return df->desktopGroup().readXdgListEntry("OnlyShowIn");
}

QStringList CaskAutostart::excludedEnvironments() const
{
    return df->desktopGroup().readXdgListEntry("NotShowIn");
}

QString CaskAutostart::startAfter() const
{
    return df->desktopGroup().readEntry("X-KDE-autostart-after");
}
