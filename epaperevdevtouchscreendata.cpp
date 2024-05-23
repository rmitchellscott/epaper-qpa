#include "epaperevdevtouchscreendata.h"

#include <QString>
#include <QLoggingCategory>
#include <QGuiApplication>
#include <QPainter>

using namespace Qt::StringLiterals;

// Set to e.g. EPTOUCH_DEBUG=/tmp/touch-debug.png to get a dump of all touch events
const QString touchDebugFile = qEnvironmentVariable("EPTOUCH_DEBUG");

Q_LOGGING_CATEGORY(epaperLcTouchScreenData, "rm.epaperevdevtouchscreendata", QtWarningMsg)
Q_LOGGING_CATEGORY(epaperLcTouchScreenDataEvents, "rm.epaperevdevtouchevents", QtWarningMsg)

EpaperEvdevTouchScreenData::EpaperEvdevTouchScreenData(const QStringList& args) :
    m_lastEventType(-1),
    m_currentSlot(0),
    hw_range_x_min(0),
    hw_range_x_max(0),
    hw_range_y_min(0),
    hw_range_y_max(0),
    hw_pressure_min(0),
    hw_pressure_max(0)
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

    m_debugOverlaySaveTimer.setSingleShot(true);
    connect(&m_debugOverlaySaveTimer, &QTimer::timeout, this, [this]() {
        if (!m_debugOverlay.save(touchDebugFile)) {
            qCWarning(epaperLcTouchScreenData, "failed to write EPTOUCH_DEBUG");
        }
    });
}

void EpaperEvdevTouchScreenData::processInputEvent(const input_event* data)
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
        } else if (data->code == ABS_MT_TOOL_TYPE) {
            Q_ASSERT(m_currentData.trackingId != -1);

            switch (data->value) {
            case MT_TOOL_FINGER:
                m_currentData.type = Contact::Type::Finger;
                break;
            case MT_TOOL_PEN:
                m_currentData.type = Contact::Type::Pen;
                break;
            case MT_TOOL_PALM:
                m_currentData.type = Contact::Type::Palm;
                break;
            default:
                m_currentData.type = Contact::Type::Unknown;
                break;
            }

            m_contacts[m_currentSlot].type = m_currentData.type;
        } else if (data->code == ABS_MT_TOUCH_MAJOR) {
            m_currentData.maj = data->value;
            if (data->value == 0)
                m_currentData.state = QEventPoint::State::Released;
            m_contacts[m_currentSlot].maj = m_currentData.maj;
        } else if (data->code == ABS_PRESSURE || data->code == ABS_MT_PRESSURE) {
            if (Q_UNLIKELY(epaperLcTouchScreenDataEvents().isDebugEnabled()))
                qCDebug(epaperLcTouchScreenDataEvents,
                        "EV_ABS code 0x%x: pressure %d; bounding to [%d,%d]",
                        data->code,
                        data->value,
                        hw_pressure_min,
                        hw_pressure_max);
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
        reportPoints();
    }

    m_lastEventType = data->type;
}

// TODO: I can't help but think this method could work better if we had a (chain of) transformers
// that consume Contact and output Contact, and ultimately at the end of that chain, we map
// to QWSI::TouchPoint. But there's some tricky details to get right there, like, how we properly
// ensure all points get killed off, etc.
//
// Something for a later day, but I expect the need will come from e.g. filtering out noise that aren't
// real events, etc.
void EpaperEvdevTouchScreenData::reportPoints()
{
    const QRect winRect = m_screenGeometry;

    // If this breaks, the driver isn't reporting ABS_MT_TRACKING_ID correctly.
    Q_ASSERT(m_contacts.isEmpty() || m_contacts.constBegin().value().trackingId != -1);

    // If this breaks, somehow, the screen size hasn't been set, or wasn't read correctly.
    Q_ASSERT(!winRect.isNull());

    const auto& makeTouchPoint = [this, &winRect](const Contact& contact) -> QWindowSystemInterface::TouchPoint {
        QWindowSystemInterface::TouchPoint tp;
        tp.id = contact.trackingId;
        tp.state = contact.state;

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

        // Map the coordinates based on the normalized position. QPA expects
        // 'area' to be in screen coordinates. Generate a screen position that
        // is always inside the active window or the primary screen.  Even
        // though we report this as a QRectF, internally Qt uses QRect/QPoint so
        // we need to bound the size to winRect.size() - QSize(1, 1)
        const int hw_w = hw_range_x_max - hw_range_x_min;
        const int hw_h = hw_range_y_max - hw_range_y_min;
        const qreal wx = winRect.left() + tp.normalPosition.x() * (winRect.width() - 1);
        const qreal wy = winRect.top() + tp.normalPosition.y() * (winRect.height() - 1);
        const qreal sizeRatio = (winRect.width() + winRect.height()) / qreal(hw_w + hw_h);
        if (tp.area.width() == -1) // touch major was not provided
            tp.area = QRectF(0, 0, 8, 8);
        else
            tp.area = QRectF(0, 0, tp.area.width() * sizeRatio, tp.area.height() * sizeRatio);
        tp.area.moveCenter(QPointF(wx, wy));

        // Calculate normalized pressure.
        if (!hw_pressure_min && !hw_pressure_max) {
            tp.pressure = tp.state == QEventPoint::State::Released ? 0 : 1;
        } else {
            tp.pressure = (tp.pressure - hw_pressure_min) / qreal(hw_pressure_max - hw_pressure_min);
        }

        return tp;
    };

    QEventPoint::States combinedStates;
    bool hasPressure = false;
    bool hasPalm = false;
    const bool touchDebug = !touchDebugFile.isEmpty();
    std::unique_ptr<QPainter> painter;

    if (touchDebug) {
        const QMargins debugOverlayMargins(100, 100, 100, 100);
        const auto& debugOverlaySize = winRect.size()
            + QSize(debugOverlayMargins.top() + debugOverlayMargins.bottom(),
                    debugOverlayMargins.left() + debugOverlayMargins.right());
        bool didResize = false;
        if (m_debugOverlay.size() != debugOverlaySize) {
            m_debugOverlay = QImage(debugOverlaySize, QImage::Format_ARGB32_Premultiplied);
            m_debugOverlay.fill(Qt::white);
            didResize = true;
        }
        painter = std::make_unique<QPainter>(&m_debugOverlay);
        const QRect debugClipRect(QPoint(debugOverlayMargins.left(), debugOverlayMargins.top()), winRect.size());
        painter->setClipRect(debugClipRect);
        painter->translate(debugClipRect.topLeft());

        if (didResize) {
            painter->drawRect(QRect(QPoint(0, 0), winRect.size()).adjusted(0, 0, -1, -1));
        }
    }

    // TODO: it would be nice to consider how we can guard against some insanity here...
    // an example might be getting a release for a not-yet-pressed point.
    // stuff like this probably points to a kernel problem, but would be useful to
    // guard userspace from them if we can.
    QList<QWindowSystemInterface::TouchPoint> touchPoints;
    for (auto it = m_contacts.begin(), end = m_contacts.end(); it != end; ++it) {
        Contact& contact(it.value());
        const auto& tp = makeTouchPoint(contact);

        if (!contact.state) {
            continue;
        }

        if (touchDebug) {
            // I don't really care about the case of multiple screens here. If it's ever an issue this needs fixing.
            Q_ASSERT(winRect.topLeft() == QPoint(0, 0));

            static int colorIndex = 0;

            std::array<const char*, 10> colors{
                "#1f77b4",
                "#ff7f0e",
                "#2ca02c",
                "#d62728",
                "#9467bd",
                "#8c564b",
                "#e377c2",
                "#7f7f7f",
                "#bcbd22",
                "#17becf",
            };

            // This is technically not perfect, as we lose the color on each subsequent press,
            // but it avoids having to store colors separately or something.
            if (tp.state == QEventPoint::Pressed) {
                colorIndex++;
                if (colorIndex >= colors.size()) {
                    colorIndex = 0;
                }
            }

            painter->fillRect(tp.area, QColor(colors[colorIndex]));
            if (tp.state == QEventPoint::Pressed) {
                painter->drawText(tp.area.bottomRight(), QString::fromLatin1("TD: %1").arg(tp.id));
            } else if (tp.state == QEventPoint::Released) {
                painter->drawText(tp.area.bottomRight(), QString::fromLatin1("TU: %1").arg(tp.id));
            }
        }

        if (contact.type == Contact::Type::Palm) {
            hasPalm = true;
        } else {
            if (contact.pressure) {
                hasPressure = true;
            }
            combinedStates |= tp.state;
            touchPoints.append(tp);
        }

        // Ensure the state is correctly reset if we just reported a release.
        // If it wasn't released, reset it to stationary, so we can detect moves next time.
        if (contact.state == QEventPoint::State::Released) {
            contact.state = QEventPoint::State::Unknown;
            contact.type = Contact::Type::Unknown;
        } else {
            contact.state = QEventPoint::State::Stationary;
        }
    }

    if (touchDebug) {
        // We throttle the save so it doesn't happen too often...
        m_debugOverlaySaveTimer.start(500);
    }

    // If there is at least one palm on the screen now, cancel the gesture.
    // While the palm is down, we won't report any touch events at all.
    // When the palm is eventually released, we'll start reporting again (in the else).
    if (hasPalm) {
        if (!m_hasPalm) {
            m_hasPalm = true;

            // Only cancel if there's an event stream active...
            if (m_touchActive) {
                qCDebug(epaperLcTouchScreenData) << "cancelling ongoing touch sequence due to palm";
                emit cancelTouch();
            }
        }
    } else {
        // Only report if something actually interesting happened...
        bool shouldReport = !touchPoints.isEmpty() && (hasPressure || combinedStates != QEventPoint::State::Stationary);

        if (m_hasPalm) {
            m_hasPalm = false;

            // If a palm was blocking the gesture, then we need to re-report all points as pressed,
            // as we sent a cancel that effectively throws out all touch state we had set before.
            if (m_touchActive) {
                qCDebug(epaperLcTouchScreenData) << "reviving previously-killed-by-palm touch sequence";
                for (auto& point : touchPoints) {
                    shouldReport = true; // force a report
                    if (point.state == QEventPoint::Stationary || point.state == QEventPoint::Updated) {
                        point.state = QEventPoint::Pressed;
                    }
                }
            }
        }

        if (shouldReport) {
            if (Q_UNLIKELY(epaperLcTouchScreenDataEvents().isDebugEnabled())) {
                qCDebug(epaperLcTouchScreenDataEvents) << "reporting" << touchPoints.size();
                for (const auto& tp : touchPoints) {
                    qCDebug(epaperLcTouchScreenDataEvents) << "reporting" << tp;
                }
            }
            emit pointsChanged(touchPoints);
        }

        m_touchActive = touchPoints.size() > 0;
    }
}
