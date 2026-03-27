// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "epaperevdevtouchmanager.h"
#include "epaperevdevtouchhandler.h"
#include "epaperevdevutil.h"

#include <QStringList>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtDeviceDiscoverySupport/private/qdevicediscovery_p.h>
#include <private/qguiapplication_p.h>
#include <private/qinputdevicemanager_p_p.h>

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(epaperLcEvdevTouch)

EpaperEvdevTouchManager::EpaperEvdevTouchManager(const QString &key, const QString &specification, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(key);

    QString spec = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS"));

    if (spec.isEmpty())
        spec = specification;

    auto parsed = EpaperEvdevUtil::parseSpecification(spec);
    m_spec = std::move(parsed.spec);

    for (const QString &device : std::as_const(parsed.devices))
        addDevice(device);

    // when no devices specified, use device discovery to scan and monitor
    if (parsed.devices.isEmpty()) {
        qCDebug(epaperLcEvdevTouch, "evdevtouch: Using device discovery");
        if (auto deviceDiscovery = QDeviceDiscovery::create(QDeviceDiscovery::Device_Touchpad | QDeviceDiscovery::Device_Touchscreen, this)) {
            const QStringList devices = deviceDiscovery->scanConnectedDevices();
            for (const QString &device : devices)
                addDevice(device);

            connect(deviceDiscovery, &QDeviceDiscovery::deviceDetected,
                    this, &EpaperEvdevTouchManager::addDevice);
            connect(deviceDiscovery, &QDeviceDiscovery::deviceRemoved,
                    this, &EpaperEvdevTouchManager::removeDevice);
        }
    }
}

EpaperEvdevTouchManager::~EpaperEvdevTouchManager()
{
}

void EpaperEvdevTouchManager::addDevice(const QString &deviceNode)
{
    qCDebug(epaperLcEvdevTouch, "evdevtouch: Adding device at %ls", qUtf16Printable(deviceNode));
    auto handler = std::make_unique<EpaperEvdevTouchScreenHandlerThread>(deviceNode, m_spec);
    if (handler) {
        connect(handler.get(), &EpaperEvdevTouchScreenHandlerThread::touchDeviceRegistered, this, &EpaperEvdevTouchManager::updateInputDeviceCount);
        m_activeDevices.add(deviceNode, std::move(handler));
    } else {
        qWarning("evdevtouch: Failed to open touch device %ls", qUtf16Printable(deviceNode));
    }
}

void EpaperEvdevTouchManager::removeDevice(const QString &deviceNode)
{
    if (m_activeDevices.remove(deviceNode)) {
        qCDebug(epaperLcEvdevTouch, "evdevtouch: Removing device at %ls", qUtf16Printable(deviceNode));
        updateInputDeviceCount();
    }
}

void EpaperEvdevTouchManager::updateInputDeviceCount()
{
    int registeredTouchDevices = 0;
    for (const auto &device : m_activeDevices) {
        if (device.handler->isPointingDeviceRegistered())
            ++registeredTouchDevices;
    }

    qCDebug(epaperLcEvdevTouch, "evdevtouch: Updating QInputDeviceManager device count: %d touch devices, %d pending handler(s)",
            registeredTouchDevices, m_activeDevices.count() - registeredTouchDevices);

    QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
        QInputDeviceManager::DeviceTypeTouch, registeredTouchDevices);
}

QT_END_NAMESPACE
