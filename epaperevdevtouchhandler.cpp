// Copyright (C) 2019 The Qt Company Ltd.
// Copyright (C) 2016 Jolla Ltd, author: <gunnar.sletta@jollamobile.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "epaperevdevtouchhandler.h"
#include "epaperevdevtouchscreendata.h"
#include <QtInputSupport/private/qoutputmapping_p.h>
#include <QStringList>
#include <QHash>
#include <QSocketNotifier>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtCore/private/qcore_unix_p.h>
#include <QtGui/qpointingdevice.h>
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/private/qpointingdevice_p.h>
#include <QtGui/private/qhighdpiscaling_p.h>

#include <QtCore/qpointer.h>

#include <mutex>

#include <math.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(epaperLcEvdevTouch, "rm.epaperevdevtouchscreenhandler", QtWarningMsg)

#define LONG_BITS (sizeof(long) << 3)
#define NUM_LONGS(bits) (((bits) + LONG_BITS - 1) / LONG_BITS)

static inline bool testBit(long bit, const long *array)
{
    return (array[bit / LONG_BITS] >> bit % LONG_BITS) & 1;
}

EpaperEvdevTouchScreenHandler::EpaperEvdevTouchScreenHandler(const QString &device, const QString &spec, QObject *parent)
    : QObject(parent), m_notify(nullptr), m_fd(-1), d(nullptr), m_device(nullptr)
{
    setObjectName("Evdev Touch Handler"_L1);
    qCDebug(epaperLcEvdevTouch, "evdevtouch: Using device %ls", qUtf16Printable(device));

    m_fd = QT_OPEN(device.toLocal8Bit().constData(), O_RDONLY | O_NDELAY, 0);

    if (m_fd >= 0) {
        m_notify = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notify, &QSocketNotifier::activated, this, &EpaperEvdevTouchScreenHandler::readData);
    } else {
        qErrnoWarning("evdevtouch: Cannot open input device %ls", qUtf16Printable(device));
        return;
    }

    const QStringList args = spec.split(u':');
    d = new EpaperEvdevTouchScreenData(args);
    connect(d, &EpaperEvdevTouchScreenData::pointsChanged, this, [this](const QList<QWindowSystemInterface::TouchPoint>& points) {
        // nullptr means QGuiApplication will pick the target window.
        QWindowSystemInterface::handleTouchEvent(nullptr, touchDevice(), points);
    });
    connect(d, &EpaperEvdevTouchScreenData::cancelTouch, this, [this]() {
        // nullptr means QGuiApplication will pick the target window.
        QWindowSystemInterface::handleTouchCancelEvent(nullptr, touchDevice());
    });

    long absbits[NUM_LONGS(ABS_CNT)];
    if (ioctl(m_fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0) {
        Q_ASSERT_X(testBit(ABS_MT_SLOT, absbits), "", "type A devices are not supported");
        Q_ASSERT_X(testBit(ABS_MT_POSITION_X, absbits), "", "single touch is not supported");
    }

    m_deviceNode = device;
    qCDebug(epaperLcEvdevTouch, "evdevtouch: %ls", qUtf16Printable(m_deviceNode));

    input_absinfo absInfo;
    memset(&absInfo, 0, sizeof(input_absinfo));
    bool has_x_range = false, has_y_range = false;

    if (ioctl(m_fd, EVIOCGABS(ABS_MT_POSITION_X), &absInfo) >= 0) {
        qCDebug(epaperLcEvdevTouch, "evdevtouch: %ls: min X: %d max X: %d", qUtf16Printable(device),
                absInfo.minimum, absInfo.maximum);
        d->hw_range_x_min = absInfo.minimum;
        d->hw_range_x_max = absInfo.maximum;
        has_x_range = true;
    }

    if (ioctl(m_fd, EVIOCGABS(ABS_MT_POSITION_Y), &absInfo) >= 0) {
        qCDebug(epaperLcEvdevTouch, "evdevtouch: %ls: min Y: %d max Y: %d", qUtf16Printable(device),
                absInfo.minimum, absInfo.maximum);
        d->hw_range_y_min = absInfo.minimum;
        d->hw_range_y_max = absInfo.maximum;
        has_y_range = true;
    }

    if (!has_x_range || !has_y_range)
        qWarning("evdevtouch: %ls: Invalid ABS limits, behavior unspecified", qUtf16Printable(device));

    if (ioctl(m_fd, EVIOCGABS(ABS_PRESSURE), &absInfo) >= 0) {
        qCDebug(epaperLcEvdevTouch, "evdevtouch: %ls: min pressure: %d max pressure: %d", qUtf16Printable(device),
                absInfo.minimum, absInfo.maximum);
        if (absInfo.maximum > absInfo.minimum) {
            d->hw_pressure_min = absInfo.minimum;
            d->hw_pressure_max = absInfo.maximum;
        }
    }

    char name[1024];
    if (ioctl(m_fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
        m_hw_name = QString::fromLocal8Bit(name);
        qCDebug(epaperLcEvdevTouch, "evdevtouch: %ls: device name: %s", qUtf16Printable(device), name);
    }

    // Fix up the coordinate ranges for am335x in case the kernel driver does not have them fixed.
    if (m_hw_name == "ti-tsc"_L1) {
        if (d->hw_range_x_min == 0 && d->hw_range_x_max == 4095) {
            d->hw_range_x_min = 165;
            d->hw_range_x_max = 4016;
        }
        if (d->hw_range_y_min == 0 && d->hw_range_y_max == 4095) {
            d->hw_range_y_min = 220;
            d->hw_range_y_max = 3907;
        }
        qCDebug(epaperLcEvdevTouch, "evdevtouch: found ti-tsc, overriding: min X: %d max X: %d min Y: %d max Y: %d",
                d->hw_range_x_min, d->hw_range_x_max, d->hw_range_y_min, d->hw_range_y_max);
    }

    bool grabSuccess = !ioctl(m_fd, EVIOCGRAB, (void *) 1);
    if (grabSuccess)
        ioctl(m_fd, EVIOCGRAB, (void *) 0);
    else
        qWarning("evdevtouch: The device is grabbed by another process. No events will be read.");

    QOutputMapping *mapping = QOutputMapping::get();
    if (mapping->load()) {
        m_screenName = mapping->screenNameForDeviceNode(m_deviceNode);
        if (!m_screenName.isEmpty())
            qCDebug(epaperLcEvdevTouch,
                    "evdevtouch: Mapping device %ls to screen %ls",
                    qUtf16Printable(m_deviceNode),
                    qUtf16Printable(m_screenName));
    }

    // In upstream evdevtouch, this is done just before reporting fingers.
    // But since we don't actually have multiple screens, I decided to just do it here instead,
    // to save some work and make life a bit easier.
    //
    // In practice, if this QPA plugin ever cares about multiple screens,
    // then I think we want to do this on screen add/remove, or geometry change.
    //
    // Original upstream comment continues:
    // Now it becomes tricky. Traditionally we picked the primaryScreen()
    // and were done with it. But then, enter multiple screens, and
    // suddenly it was all broken.
    //
    // For now we only support the display configuration of the KMS/DRM
    // backends of eglfs. See QOutputMapping.
    //
    // The good news it that once winRect refers to the correct screen
    // geometry in the full virtual desktop space, there is nothing else
    // left to do since qguiapp will handle the rest.
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!m_screenName.isEmpty()) {
        if (!m_screen) {
            const QList<QScreen*> screens = QGuiApplication::screens();
            for (QScreen* s : screens) {
                if (s->name() == m_screenName) {
                    m_screen = s;
                    break;
                }
            }
        }
        if (m_screen)
            screen = m_screen;
    }
    d->m_screenGeometry = screen ? QHighDpi::toNativePixels(screen->geometry(), screen) : QRect();

    // For the benefit of helping write test scenarios.
    const bool dumpDataParameters = true;
    if (dumpDataParameters) {
        qWarning(epaperLcEvdevTouch) << "xmin" << d->hw_range_x_min;
        qWarning(epaperLcEvdevTouch) << "xmax" << d->hw_range_x_max;
        qWarning(epaperLcEvdevTouch) << "ymin" << d->hw_range_y_min;
        qWarning(epaperLcEvdevTouch) << "ymax" << d->hw_range_y_max;
        qWarning(epaperLcEvdevTouch) << "pmin" << d->hw_pressure_min;
        qWarning(epaperLcEvdevTouch) << "pmax" << d->hw_pressure_max;
        qWarning(epaperLcEvdevTouch) << "rotate" << d->m_rotate;
        qWarning(epaperLcEvdevTouch) << "screenGeometry" << d->m_screenGeometry;
    }

    registerPointingDevice();
}

EpaperEvdevTouchScreenHandler::~EpaperEvdevTouchScreenHandler()
{
    if (m_fd >= 0)
        QT_CLOSE(m_fd);

    delete d;

    unregisterPointingDevice();
}

QPointingDevice *EpaperEvdevTouchScreenHandler::touchDevice() const
{
    return m_device;
}

void EpaperEvdevTouchScreenHandler::readData()
{
    ::input_event buffer[32];
    int events = 0;

    int n = 0;
    for (; ;) {
        events = QT_READ(m_fd, reinterpret_cast<char*>(buffer) + n, sizeof(buffer) - n);
        if (events <= 0)
            goto err;
        n += events;
        if (n % sizeof(::input_event) == 0)
            break;
    }

    n /= sizeof(::input_event);

    for (int i = 0; i < n; ++i)
        d->processInputEvent(&buffer[i]);
    return;

err:
    if (!events) {
        qWarning("evdevtouch: Got EOF from input device");
        return;
    } else if (events < 0) {
        if (errno != EINTR && errno != EAGAIN) {
            qErrnoWarning("evdevtouch: Could not read from input device");
            if (errno == ENODEV) { // device got disconnected -> stop reading
                delete m_notify;
                m_notify = nullptr;

                QT_CLOSE(m_fd);
                m_fd = -1;

                unregisterPointingDevice();
            }
            return;
        }
    }
}

void EpaperEvdevTouchScreenHandler::registerPointingDevice()
{
    if (m_device)
        return;

    static int id = 1;
    QPointingDevice::Capabilities caps = QPointingDevice::Capability::Position | QPointingDevice::Capability::Area;
    if (d->hw_pressure_max > d->hw_pressure_min)
        caps.setFlag(QPointingDevice::Capability::Pressure);

    // TODO get evdev ID instead of an incremeting number; set USB ID too
    m_device = new QPointingDevice(
        m_hw_name, id++, QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger, caps, 16, 0);

    auto geom = d->m_screenGeometry;
    if (!geom.isNull())
        QPointingDevicePrivate::get(m_device)->setAvailableVirtualGeometry(geom);

    QWindowSystemInterface::registerInputDevice(m_device);
}

/*! \internal

    EpaperEvdevTouchScreenHandler::unregisterPointingDevice can be called by several cases.

    First of all, the case that an application is terminated, and destroy all input devices
    immediately to unregister in this case.

    Secondly, the case that removing a device without touch events for the device while the
    application is still running. In this case, the destructor of EpaperEvdevTouchScreenHandler from
    the connection with QDeviceDiscovery::deviceRemoved in EpaperEvdevTouchManager calls this method.
    And this method moves a device into the main thread and then deletes it later but there is no
    touch events for the device so that the device would be deleted in appropriate time.

    Finally, this case is similar as the second one but with touch events, that is, a device is
    removed while touch events are given to the device and the application is still running.
    In this case, this method is called by readData with ENODEV error and the destructor of
    EpaperEvdevTouchScreenHandler. So in order to prevent accessing the device which is already nullptr,
    check the nullity of a device first. And as same as the second case, move the device into the
    main thread and then delete it later. But in this case, cannot guarantee which event is
    handled first since the list or queue where posting QDeferredDeleteEvent and appending touch
    events are different.
    If touch events are handled first, there is no problem because the device which is used for
    these events is registered. However if QDeferredDeleteEvent for deleting the device is
    handled first, this may cause a crash due to using unregistered device when processing touch
    events later. In order to prevent processing such touch events, check a device which is used
    for touch events is registered when processing touch events.

    see QGuiApplicationPrivate::processTouchEvent().
 */
void EpaperEvdevTouchScreenHandler::unregisterPointingDevice()
{
    if (!m_device)
        return;

    if (QGuiApplication::instance()) {
        m_device->moveToThread(QGuiApplication::instance()->thread());
        m_device->deleteLater();
    } else {
        delete m_device;
    }
    m_device = nullptr;
}

EpaperEvdevTouchScreenHandlerThread::EpaperEvdevTouchScreenHandlerThread(const QString &device, const QString &spec, QObject *parent)
    : QDaemonThread(parent), m_device(device), m_spec(spec), m_handler(nullptr), m_touchDeviceRegistered(false)
{
    start();
}

EpaperEvdevTouchScreenHandlerThread::~EpaperEvdevTouchScreenHandlerThread()
{
    quit();
    wait();
}

void EpaperEvdevTouchScreenHandlerThread::run()
{
    m_handler = new EpaperEvdevTouchScreenHandler(m_device, m_spec);

    // Report the registration to the parent thread by invoking the method asynchronously
    QMetaObject::invokeMethod(this, "notifyTouchDeviceRegistered", Qt::QueuedConnection);

    exec();

    delete m_handler;
    m_handler = nullptr;
}

bool EpaperEvdevTouchScreenHandlerThread::isPointingDeviceRegistered() const
{
    return m_touchDeviceRegistered;
}

void EpaperEvdevTouchScreenHandlerThread::notifyTouchDeviceRegistered()
{
    m_touchDeviceRegistered = true;
    emit touchDeviceRegistered();
}

QT_END_NAMESPACE
