#pragma once
#include <QObject>
#include <qpa/qwindowsysteminterface.h>

#include <linux/input.h>

class EpaperEvdevTouchScreenData : public QObject
{
    Q_OBJECT
public:
    EpaperEvdevTouchScreenData(const QStringList& args);

    void processInputEvent(const input_event* data);

signals:
    void pointsChanged(const QList<QWindowSystemInterface::TouchPoint>& points);

public:
    int m_lastEventType;

    struct Contact
    {
        int trackingId = -1;
        int x = 0;
        int y = 0;
        int maj = -1;
        int pressure = 0;
        QEventPoint::State state = QEventPoint::State::Pressed;
    };

    QHash<int, Contact> m_contacts; // The key is a slot number for type B. Type A is unsupported.
    Contact m_currentData;
    int m_currentSlot;

    void addTouchPoint(const Contact& contact, QEventPoint::States* combinedStates);
    void reportPoints();
    void loadMultiScreenMappings();

    int hw_range_x_min;
    int hw_range_x_max;
    int hw_range_y_min;
    int hw_range_y_max;
    int hw_pressure_min;
    int hw_pressure_max;
    QTransform m_rotate;
    QRect m_screenGeometry;
};
