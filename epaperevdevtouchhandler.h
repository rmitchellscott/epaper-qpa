// Copyright (C) 2016 The Qt Company Ltd.
// Copyright (C) 2016 Jolla Ltd, author: <gunnar.sletta@jollamobile.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef EPAPEREVDEVTOUCHHANDLER_P_H
#define EPAPEREVDEVTOUCHHANDLER_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

//#include <QtGui/qpointingdevice.h>
#include <QtGui/private/qtguiglobal_p.h>
#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QThread>
#include <QtCore/private/qthread_p.h>
#include <qpa/qwindowsysteminterface.h>
#include "epaperevdevtouchfilter.h"

QT_BEGIN_NAMESPACE

class QSocketNotifier;
class EpaperEvdevTouchScreenData;
class QPointingDevice;

class EpaperEvdevTouchScreenHandler : public QObject
{
    Q_OBJECT

public:
    explicit EpaperEvdevTouchScreenHandler(const QString &device, const QString &spec = QString(), QObject *parent = nullptr);
    ~EpaperEvdevTouchScreenHandler();

    QPointingDevice *touchDevice() const;

    bool isFiltered() const;

    void readData();

signals:
    void touchPointsUpdated();

private:
    friend class EpaperEvdevTouchScreenData;
    friend class EpaperEvdevTouchScreenHandlerThread;

    void registerPointingDevice();
    void unregisterPointingDevice();

    QSocketNotifier *m_notify;
    int m_fd;
    EpaperEvdevTouchScreenData *d;
    QPointingDevice *m_device;
};

class EpaperEvdevTouchScreenHandlerThread : public QDaemonThread
{
    Q_OBJECT
public:
    explicit EpaperEvdevTouchScreenHandlerThread(const QString &device, const QString &spec, QObject *parent = nullptr);
    ~EpaperEvdevTouchScreenHandlerThread();
    void run() override;

    bool isPointingDeviceRegistered() const;

    bool eventFilter(QObject *object, QEvent *event) override;

    void scheduleTouchPointUpdate();

signals:
    void touchDeviceRegistered();

private:
    Q_INVOKABLE void notifyTouchDeviceRegistered();

    void filterAndSendTouchPoints();
    QRect targetScreenGeometry() const;

    QString m_device;
    QString m_spec;
    EpaperEvdevTouchScreenHandler *m_handler;
    bool m_touchDeviceRegistered;

    bool m_touchUpdatePending;
    QWindow *m_filterWindow;

    struct FilteredTouchPoint {
        EpaperEvdevTouchFilter x;
        EpaperEvdevTouchFilter y;
        QWindowSystemInterface::TouchPoint touchPoint;
    };
    QHash<int, FilteredTouchPoint> m_filteredPoints;

    float m_touchRate;
};

QT_END_NAMESPACE

#endif // EPAPEREVDEVTOUCHHANDLER_P_H
