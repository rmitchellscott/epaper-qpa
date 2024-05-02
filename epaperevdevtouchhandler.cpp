// Copyright (C) 2019 The Qt Company Ltd.
// Copyright (C) 2016 Jolla Ltd, author: <gunnar.sletta@jollamobile.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "epaperevdevtouchhandler.h"
#include <QtInputSupport/private/qoutputmapping_p.h>
#include <QStringList>
#include <QHash>
#include <QSocketNotifier>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtCore/private/qcore_unix_p.h>
#include <QtGui/qpointingdevice.h>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/private/qpointingdevice_p.h>

#include <QtCore/qpointer.h>

#include <mutex>

#include <linux/input.h>

#include <math.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(epaperLcEvdevTouch, "qt.qpa.input")
Q_LOGGING_CATEGORY(epaperLcEvents, "qt.qpa.input.events")

class EpaperEvdevTouchScreenData
{
public:
    EpaperEvdevTouchScreenData(EpaperEvdevTouchScreenHandler *q_ptr, const QStringList &args);

    void processInputEvent(input_event *data);
    void assignIds();

    EpaperEvdevTouchScreenHandler *q;
    int m_lastEventType;
    QList<QWindowSystemInterface::TouchPoint> m_touchPoints;
    QList<QWindowSystemInterface::TouchPoint> m_lastTouchPoints;

    struct Contact {
        int trackingId = -1;
        int x = 0;
        int y = 0;
        int maj = -1;
        int pressure = 0;
        QEventPoint::State state = QEventPoint::State::Pressed;
    };
    QHash<int, Contact> m_contacts; // The key is a tracking id for type A, slot number for type B.
    QHash<int, Contact> m_lastContacts;
    Contact m_currentData;
    int m_currentSlot;

    int findClosestContact(const QHash<int, Contact> &contacts, int x, int y, int *dist);
    void addTouchPoint(const Contact &contact, QEventPoint::States *combinedStates);
    void reportPoints();
    void loadMultiScreenMappings();

    QRect screenGeometry() const;

    int hw_range_x_min;
    int hw_range_x_max;
    int hw_range_y_min;
    int hw_range_y_max;
    int hw_pressure_min;
    int hw_pressure_max;
    QString hw_name;
    QString deviceNode;
    QTransform m_rotate;
    QString m_screenName;
    mutable QPointer<QScreen> m_screen;
};

EpaperEvdevTouchScreenData::EpaperEvdevTouchScreenData(EpaperEvdevTouchScreenHandler *q_ptr, const QStringList &args)
    : q(q_ptr),
      m_lastEventType(-1),
      m_currentSlot(0),
      hw_range_x_min(0), hw_range_x_max(0),
      hw_range_y_min(0), hw_range_y_max(0),
      hw_pressure_min(0), hw_pressure_max(0)
{
    int rotationAngle = 0;
    bool invertx = false;
    bool inverty = false;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i).startsWith("rotate"_L1)) {
            QString rotateArg = args.at(i).section(u'=', 1, 1);
            bool ok;
            uint argValue = rotateArg.toUInt(&ok);
            if (ok) {
                switch (argValue) {
                case 90:
                case 180:
                case 270:
                    rotationAngle = argValue;
                    break;
                default:
                    break;
                }
            }
        } else if (args.at(i) == "invertx"_L1) {
            invertx = true;
        } else if (args.at(i) == "inverty"_L1) {
            inverty = true;
        }
    }

    if (rotationAngle) {
        m_rotate = QTransform::fromTranslate(0.5, 0.5).rotate(rotationAngle).translate(-0.5, -0.5);
    }

    if (invertx) {
        m_rotate *= QTransform::fromTranslate(0.5, 0.5).scale(-1.0, 1.0).translate(-0.5, -0.5);
    }

    if (inverty) {
        m_rotate *= QTransform::fromTranslate(0.5, 0.5).scale(1.0, -1.0).translate(-0.5, -0.5);
    }
}

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
    d = new EpaperEvdevTouchScreenData(this, args);

    long absbits[NUM_LONGS(ABS_CNT)];
    if (ioctl(m_fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0) {
        Q_ASSERT_X(testBit(ABS_MT_SLOT, absbits), "", "type A devices are not supported");
        Q_ASSERT_X(testBit(ABS_MT_POSITION_X, absbits), "", "single touch is not supported");
    }

    d->deviceNode = device;
    qCDebug(epaperLcEvdevTouch,
            "evdevtouch: %ls",
            qUtf16Printable(d->deviceNode));

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
        d->hw_name = QString::fromLocal8Bit(name);
        qCDebug(epaperLcEvdevTouch, "evdevtouch: %ls: device name: %s", qUtf16Printable(device), name);
    }

    // Fix up the coordinate ranges for am335x in case the kernel driver does not have them fixed.
    if (d->hw_name == "ti-tsc"_L1) {
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
        d->m_screenName = mapping->screenNameForDeviceNode(d->deviceNode);
        if (!d->m_screenName.isEmpty())
            qCDebug(epaperLcEvdevTouch, "evdevtouch: Mapping device %ls to screen %ls",
                    qUtf16Printable(d->deviceNode), qUtf16Printable(d->m_screenName));
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
    m_device = new QPointingDevice(d->hw_name, id++,
                                   QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger,
                                   caps, 16, 0);

    auto geom = d->screenGeometry();
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

void EpaperEvdevTouchScreenData::addTouchPoint(const Contact &contact, QEventPoint::States *combinedStates)
{
    QWindowSystemInterface::TouchPoint tp;
    tp.id = contact.trackingId;
    tp.state = contact.state;
    *combinedStates |= tp.state;

    // Store the HW coordinates for now, will be updated later.
    tp.area = QRectF(0, 0, contact.maj, contact.maj);
    tp.area.moveCenter(QPoint(contact.x, contact.y));
    tp.pressure = contact.pressure;

    // Get a normalized position in range 0..1.
    tp.normalPosition = QPointF((contact.x - hw_range_x_min) / qreal(hw_range_x_max - hw_range_x_min),
                                (contact.y - hw_range_y_min) / qreal(hw_range_y_max - hw_range_y_min));

    if (!m_rotate.isIdentity())
        tp.normalPosition = m_rotate.map(tp.normalPosition);

    tp.rawPositions.append(QPointF(contact.x, contact.y));

    m_touchPoints.append(tp);
}

void EpaperEvdevTouchScreenData::processInputEvent(input_event *data)
{
    if (data->type == EV_ABS) {

        if (data->code == ABS_MT_POSITION_X) {
            m_currentData.x = qBound(hw_range_x_min, data->value, hw_range_x_max);
            m_contacts[m_currentSlot].x = m_currentData.x;
            if (m_contacts[m_currentSlot].state == QEventPoint::State::Stationary)
                m_contacts[m_currentSlot].state = QEventPoint::State::Updated;
        } else if (data->code == ABS_MT_POSITION_Y) {
            m_currentData.y = qBound(hw_range_y_min, data->value, hw_range_y_max);
            m_contacts[m_currentSlot].y = m_currentData.y;
            if (m_contacts[m_currentSlot].state == QEventPoint::State::Stationary)
                m_contacts[m_currentSlot].state = QEventPoint::State::Updated;
        } else if (data->code == ABS_MT_TRACKING_ID) {
            m_currentData.trackingId = data->value;
            if (m_currentData.trackingId == -1) {
                m_contacts[m_currentSlot].state = QEventPoint::State::Released;
            } else {
                m_contacts[m_currentSlot].state = QEventPoint::State::Pressed;
                m_contacts[m_currentSlot].trackingId = m_currentData.trackingId;
            }
        } else if (data->code == ABS_MT_TOUCH_MAJOR) {
            m_currentData.maj = data->value;
            if (data->value == 0)
                m_currentData.state = QEventPoint::State::Released;
            m_contacts[m_currentSlot].maj = m_currentData.maj;
        } else if (data->code == ABS_PRESSURE || data->code == ABS_MT_PRESSURE) {
            if (Q_UNLIKELY(epaperLcEvents().isDebugEnabled()))
                qCDebug(epaperLcEvents, "EV_ABS code 0x%x: pressure %d; bounding to [%d,%d]",
                        data->code, data->value, hw_pressure_min, hw_pressure_max);
            m_currentData.pressure = qBound(hw_pressure_min, data->value, hw_pressure_max);
            m_contacts[m_currentSlot].pressure = m_currentData.pressure;
        } else if (data->code == ABS_MT_SLOT) {
            m_currentSlot = data->value;
        }

    } else if (data->type == EV_SYN && data->code == SYN_MT_REPORT && m_lastEventType != EV_SYN) {

        // If there is no tracking id, one will be generated later.
        // Until that use a temporary key.
        int key = m_currentData.trackingId;
        if (key == -1)
            key = m_contacts.size();

        m_contacts.insert(key, m_currentData);
        m_currentData = Contact();

    } else if (data->type == EV_SYN && data->code == SYN_REPORT) {

        // Ensure valid IDs even when the driver does not report ABS_MT_TRACKING_ID.
        if (!m_contacts.isEmpty() && m_contacts.constBegin().value().trackingId == -1)
            assignIds();

        m_lastTouchPoints = m_touchPoints;
        m_touchPoints.clear();
        QEventPoint::States combinedStates;
        bool hasPressure = false;

        for (auto it = m_contacts.begin(), end = m_contacts.end(); it != end; /*erasing*/) {
            Contact &contact(it.value());

            if (!contact.state) {
                ++it;
                continue;
            }

            if (contact.pressure)
                hasPressure = true;

            addTouchPoint(contact, &combinedStates);
            ++it;
        }

        // Now look for contacts that have disappeared since the last sync.
        for (auto it = m_lastContacts.begin(), end = m_lastContacts.end(); it != end; ++it) {
            Contact &contact(it.value());
            int key = it.key();
            if (contact.trackingId != m_contacts[key].trackingId && contact.state) {
                contact.state = QEventPoint::State::Released;
                addTouchPoint(contact, &combinedStates);
            }
        }

        // Remove contacts that have just been reported as released.
        for (auto it = m_contacts.begin(), end = m_contacts.end(); it != end; /*erasing*/) {
            Contact &contact(it.value());

            if (!contact.state) {
                ++it;
                continue;
            }

            if (contact.state == QEventPoint::State::Released) {
                contact.state = QEventPoint::State::Unknown;
            } else {
                contact.state = QEventPoint::State::Stationary;
            }
            ++it;
        }

        m_lastContacts = m_contacts;

        if (!m_touchPoints.isEmpty() && (hasPressure || combinedStates != QEventPoint::State::Stationary))
            reportPoints();
    }

    m_lastEventType = data->type;
}

int EpaperEvdevTouchScreenData::findClosestContact(const QHash<int, Contact> &contacts, int x, int y, int *dist)
{
    int minDist = -1, id = -1;
    for (QHash<int, Contact>::const_iterator it = contacts.constBegin(), ite = contacts.constEnd();
         it != ite; ++it) {
        const Contact &contact(it.value());
        int dx = x - contact.x;
        int dy = y - contact.y;
        int dist = dx * dx + dy * dy;
        if (minDist == -1 || dist < minDist) {
            minDist = dist;
            id = contact.trackingId;
        }
    }
    if (dist)
        *dist = minDist;
    return id;
}

void EpaperEvdevTouchScreenData::assignIds()
{
    QHash<int, Contact> candidates = m_lastContacts, pending = m_contacts, newContacts;
    int maxId = -1;
    QHash<int, Contact>::iterator it, ite, bestMatch;
    while (!pending.isEmpty() && !candidates.isEmpty()) {
        int bestDist = -1, bestId = 0;
        for (it = pending.begin(), ite = pending.end(); it != ite; ++it) {
            int dist;
            int id = findClosestContact(candidates, it->x, it->y, &dist);
            if (id >= 0 && (bestDist == -1 || dist < bestDist)) {
                bestDist = dist;
                bestId = id;
                bestMatch = it;
            }
        }
        if (bestDist >= 0) {
            bestMatch->trackingId = bestId;
            newContacts.insert(bestId, *bestMatch);
            candidates.remove(bestId);
            pending.erase(bestMatch);
            if (bestId > maxId)
                maxId = bestId;
        }
    }
    if (candidates.isEmpty()) {
        for (it = pending.begin(), ite = pending.end(); it != ite; ++it) {
            it->trackingId = ++maxId;
            newContacts.insert(it->trackingId, *it);
        }
    }
    m_contacts = newContacts;
}

QRect EpaperEvdevTouchScreenData::screenGeometry() const
{
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
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!m_screenName.isEmpty()) {
        if (!m_screen) {
            const QList<QScreen *> screens = QGuiApplication::screens();
            for (QScreen *s : screens) {
                if (s->name() == m_screenName) {
                    m_screen = s;
                    break;
                }
            }
        }
        if (m_screen)
            screen = m_screen;
    }
    return screen ? QHighDpi::toNativePixels(screen->geometry(), screen) : QRect();
}

void EpaperEvdevTouchScreenData::reportPoints()
{
    QRect winRect = screenGeometry();
    if (winRect.isNull())
        return;

    const int hw_w = hw_range_x_max - hw_range_x_min;
    const int hw_h = hw_range_y_max - hw_range_y_min;

    // Map the coordinates based on the normalized position. QPA expects 'area'
    // to be in screen coordinates.
    const int pointCount = m_touchPoints.size();
    for (int i = 0; i < pointCount; ++i) {
        QWindowSystemInterface::TouchPoint &tp(m_touchPoints[i]);

        // Generate a screen position that is always inside the active window
        // or the primary screen.  Even though we report this as a QRectF, internally
        // Qt uses QRect/QPoint so we need to bound the size to winRect.size() - QSize(1, 1)
        const qreal wx = winRect.left() + tp.normalPosition.x() * (winRect.width() - 1);
        const qreal wy = winRect.top() + tp.normalPosition.y() * (winRect.height() - 1);
        const qreal sizeRatio = (winRect.width() + winRect.height()) / qreal(hw_w + hw_h);
        if (tp.area.width() == -1) // touch major was not provided
            tp.area = QRectF(0, 0, 8, 8);
        else
            tp.area = QRectF(0, 0, tp.area.width() * sizeRatio, tp.area.height() * sizeRatio);
        tp.area.moveCenter(QPointF(wx, wy));

        // Calculate normalized pressure.
        if (!hw_pressure_min && !hw_pressure_max)
            tp.pressure = tp.state == QEventPoint::State::Released ? 0 : 1;
        else
            tp.pressure = (tp.pressure - hw_pressure_min) / qreal(hw_pressure_max - hw_pressure_min);

        if (Q_UNLIKELY(epaperLcEvents().isDebugEnabled()))
            qCDebug(epaperLcEvents) << "reporting" << tp;
    }

    // nullptr means QGuiApplication will pick the target window.
    QWindowSystemInterface::handleTouchEvent(nullptr, q->touchDevice(), m_touchPoints);
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

