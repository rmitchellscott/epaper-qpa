#include "epaperevdevtouchscreendata.h"

#include <QString>
#include <QLoggingCategory>
#include <QGuiApplication>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(epaperLcEvents, "qt.qpa.input.events")

EpaperEvdevTouchScreenData::EpaperEvdevTouchScreenData(const QStringList &args)
    : m_lastEventType(-1),
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

void EpaperEvdevTouchScreenData::processInputEvent(const input_event *data)
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

void EpaperEvdevTouchScreenData::reportPoints()
{
    QRect winRect = m_screenGeometry;
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

    emit pointsChanged(m_touchPoints);
}

