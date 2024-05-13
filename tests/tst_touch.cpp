#include <qtest.h>
#include "epaperevdevtouchscreendata.h"

class TouchTest : public QObject
{
    Q_OBJECT
private slots:
    void test_data();
    void test();
};

struct TouchStreamBuilder
{
    TouchStreamBuilder& mtX(timeval tt, int32_t value)
    {
        m_events.append({.time = tt, .type = EV_ABS, .code = ABS_MT_POSITION_X, value = value});
        return *this;
    }

    TouchStreamBuilder& mtY(timeval tt, int32_t value)
    {
        m_events.append({.time = tt, .type = EV_ABS, .code = ABS_MT_POSITION_Y, value = value});
        return *this;
    }

    TouchStreamBuilder& mtId(timeval tt, int32_t value)
    {
        m_events.append({.time = tt, .type = EV_ABS, .code = ABS_MT_TRACKING_ID, value = value});
        return *this;
    }

    TouchStreamBuilder& mtTouchMajor(timeval tt, int32_t value)
    {
        m_events.append({.time = tt, .type = EV_ABS, .code = ABS_MT_TOUCH_MAJOR, value = value});
        return *this;
    }

    TouchStreamBuilder& mtToolType(timeval tt, int32_t value)
    {
        m_events.append({.time = tt, .type = EV_ABS, .code = ABS_MT_TOOL_TYPE, value = value});
        return *this;
    }

    TouchStreamBuilder& mtPressure(timeval tt, int32_t value)
    {
        m_events.append({.time = tt, .type = EV_ABS, .code = ABS_MT_PRESSURE, value = value});
        return *this;
    }

    TouchStreamBuilder& mtSlot(timeval tt, int32_t value)
    {
        m_events.append({.time = tt, .type = EV_ABS, .code = ABS_MT_SLOT, value = value});
        return *this;
    }

    TouchStreamBuilder& synMt(timeval tt)
    {
        m_events.append({.time = tt, .type = EV_SYN, .code = SYN_MT_REPORT, 0});
        return *this;
    }

    TouchStreamBuilder& synReport(timeval tt)
    {
        m_events.append({.time = tt, .type = EV_SYN, .code = SYN_REPORT, 0});
        return *this;
    }

    QList<input_event> build()
    {
        QList<input_event> tmp;
        std::swap(m_events, tmp);
        return tmp;
    }

    static QList<input_event> fromEvtestOutput(const QByteArray& evtestOutput);

private:
    QList<input_event> m_events;
};

QList<input_event> TouchStreamBuilder::fromEvtestOutput(const QByteArray& evtestOutput)
{
    TouchStreamBuilder builder;

    const auto& getMatches = [](const QString& line, auto regex) -> std::optional<QStringList> {
        QRegularExpression re(regex);
        const auto& match = re.match(line);
        if (!match.hasMatch()) {
            return {};
        }

        return match.capturedTexts();
    };

    for (const auto& line : evtestOutput.split('\n')) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        if (auto matches = getMatches(
                line, R"(Event: time (\d+\.\d+), type 3 \(EV_ABS\), code 57 \(ABS_MT_TRACKING_ID\), value (\-?\d+))");
            matches) {
            // match(1) is the time, but we don't use that now.
            int32_t value = matches->at(2).toInt();
            builder.mtId({}, value);
            continue;
        }

        if (auto matches = getMatches(
                line, R"(Event: time (\d+\.\d+), type 3 \(EV_ABS\), code 55 \(ABS_MT_TOOL_TYPE\), value (\-?\d+))");
            matches) {
            // match(1) is the time, but we don't use that now.
            int32_t value = matches->at(2).toInt();
            builder.mtToolType({}, value);
            continue;
        }

        if (auto matches = getMatches(
                line, R"(Event: time (\d+\.\d+), type 3 \(EV_ABS\), code 53 \(ABS_MT_POSITION_X\), value (\d+))");
            matches) {
            // match(1) is the time, but we don't use that now.
            int32_t value = matches->at(2).toInt();
            builder.mtX({}, value);
            continue;
        }

        if (auto matches = getMatches(
                line, R"(Event: time (\d+\.\d+), type 3 \(EV_ABS\), code 54 \(ABS_MT_POSITION_Y\), value (\d+))");
            matches) {
            // match(1) is the time, but we don't use that now.
            int32_t value = matches->at(2).toInt();
            builder.mtY({}, value);
            continue;
        }

        if (auto matches =
                getMatches(line, R"(Event: time (\d+\.\d+), type 3 \(EV_ABS\), code 47 \(ABS_MT_SLOT\), value (\d+))");
            matches) {
            // match(1) is the time, but we don't use that now.
            int32_t value = matches->at(2).toInt();
            builder.mtSlot({}, value);
            continue;
        }

        if (auto matches = getMatches(
                line, R"(Event: time (\d+\.\d+), type 3 \(EV_ABS\), code 48 \(ABS_MT_TOUCH_MAJOR\), value (\d+))");
            matches) {
            // match(1) is the time, but we don't use that now.
            int32_t value = matches->at(2).toInt();
            builder.mtTouchMajor({}, value);
            continue;
        }

        if (auto matches = getMatches(line, R"(Event: time (\d+\.\d+), -------------- SYN_REPORT ------------)");
            matches) {
            // match(1) is the time, but we don't use that now.
            builder.synReport({});
            continue;
        }

        if (auto matches = getMatches(line, R"(Event: time (\d+\.\d+), type 1 \(EV_KEY\), .+)"); matches) {
            // we don't care about EV_KEY
            continue;
        }

        qWarning() << "unparsed line" << line;
        qFatal("fix the parser");
    }

    return builder.build();
}

struct TouchPointBuilder
{
    TouchPointBuilder& id(int value)
    {
        m_point.id = value;
        return *this;
    }

    TouchPointBuilder& uniqueId(qint64 value)
    {
        m_point.uniqueId = value;
        return *this;
    }

    TouchPointBuilder& normalPosition(QPointF value)
    {
        m_point.normalPosition = value;
        return *this;
    }

    TouchPointBuilder& area(QRectF value)
    {
        m_point.area = value;
        return *this;
    }

    TouchPointBuilder& pressure(qreal value)
    {
        m_point.pressure = value;
        return *this;
    }

    TouchPointBuilder& rotation(qreal value)
    {
        m_point.rotation = value;
        return *this;
    }

    TouchPointBuilder& state(QEventPoint::State value)
    {
        m_point.state = value;
        return *this;
    }

    TouchPointBuilder& velocity(QVector2D value)
    {
        m_point.velocity = value;
        return *this;
    }

    TouchPointBuilder& appendRawPosition(QPointF value)
    {
        m_point.rawPositions.append(value);
        return *this;
    }

    QWindowSystemInterface::TouchPoint build()
    {
        QWindowSystemInterface::TouchPoint tmp;
        std::swap(m_point, tmp);
        return tmp;
    }

private:
    QWindowSystemInterface::TouchPoint m_point;
};

std::optional<QString> pointsEqualOrError(const QWindowSystemInterface::TouchPoint lhs,
                                          const QWindowSystemInterface::TouchPoint& rhs)
{
    if (lhs.id != rhs.id) {
        return QString::fromLatin1("id mismatch");
    }
    if (lhs.uniqueId != rhs.uniqueId) {
        return QString::fromLatin1("uniqueId mismatch");
    }
    if (lhs.normalPosition.toPoint() != rhs.normalPosition.toPoint()) {
        return QString::fromLatin1("normalPosition mismatch");
    }
    if (lhs.area.toRect() != rhs.area.toRect()) {
        return QString::fromLatin1("area mismatch");
    }
    const double pressureEpsilon = 0.01;
    if (std::abs(lhs.pressure - rhs.pressure) >= pressureEpsilon) {
        return QString::fromLatin1("pressure mismatch");
    }
    const double rotationEpsilon = 0.01;
    if (std::abs(lhs.rotation - rhs.rotation) >= rotationEpsilon) {
        return QString::fromLatin1("rotation mismatch");
    }
    if (lhs.state != rhs.state) {
        return QString::fromLatin1("state mismatch");
    }
    if (lhs.velocity.toPoint() != rhs.velocity.toPoint()) {
        return QString::fromLatin1("velocity mismatch");
    }
    if (lhs.rawPositions.size() != rhs.rawPositions.size()) {
        return QString::fromLatin1("rawPositions size mismatch");
    }
    for (int i = 0; i < lhs.rawPositions.size(); ++i) {
        if (lhs.rawPositions.at(i).toPoint() != rhs.rawPositions.at(i).toPoint()) {
            return QString::fromLatin1("rawPositions mismatch at %1").arg(i);
        }
    }
    if (lhs.velocity.toPoint() != rhs.velocity.toPoint()) {
        return QString::fromLatin1("velocity mismatch");
    }

    return {};
}

struct TouchTestEvent
{
private:
    TouchTestEvent()
    {
    }

public:
    TouchTestEvent(const QList<QWindowSystemInterface::TouchPoint>& points) :
        m_points(points)
    {
    }

    TouchTestEvent(const QWindowSystemInterface::TouchPoint& point) :
        m_points({point})
    {
    }

    static TouchTestEvent cancel()
    {
        return {};
    }

    bool isCancel() const
    {
        return !m_points.has_value();
    }

    bool isTouch() const
    {
        return m_points.has_value();
    }

    QList<QWindowSystemInterface::TouchPoint> points() const
    {
        Q_ASSERT(!isCancel());
        return m_points.value();
    }

private:
    // In the case of a 'cancel' event, the points will !has_value.
    std::optional<QList<QWindowSystemInterface::TouchPoint>> m_points;
};

void TouchTest::test_data()
{
    QTest::addColumn<QList<input_event>>("inputEvents");
    QTest::addColumn<QList<TouchTestEvent>>("touchEvents");

    QTest::addRow("no-events") << TouchStreamBuilder().build() << QList<TouchTestEvent>{};

    // A single finger press and release
    QTest::addRow("ferrari-single-press-release")
        << TouchStreamBuilder::fromEvtestOutput(
               R"(
Event: time 1714664083.841958, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value 259
Event: time 1714664083.841958, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 404
Event: time 1714664083.841958, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 1405
Event: time 1714664083.841958, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 8
Event: time 1714664083.841958, -------------- SYN_REPORT ------------
Event: time 1714664083.898798, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 12
Event: time 1714664083.898798, -------------- SYN_REPORT ------------
Event: time 1714664083.974675, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value -1
Event: time 1714664083.974675, -------------- SYN_REPORT ------------
)")
        << QList<TouchTestEvent>{{TouchPointBuilder()
                                      .id(259)
                                      .area({313.809, 1068.03, 6.17647, 6.17647})
                                      .normalPosition({0.195736, 0.142655})
                                      .state(QEventPoint::State::Pressed)
                                      .appendRawPosition({404, 1405})
                                      .pressure(1.0)
                                      .build()},
                                 {TouchPointBuilder()
                                      .id(259)
                                      .area({312.265, 1066.48, 9.26471, 9.26471})
                                      .normalPosition({0.195736, 0.142655})
                                      .state(QEventPoint::State::Released)
                                      .appendRawPosition({404, 1405})
                                      .pressure(0.0)
                                      .build()}};

    // A single palm press and release
    QTest::addRow("ferrari-palm-press-release") << TouchStreamBuilder::fromEvtestOutput(
        R"(
Event: time 1715601393.060013, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value 0
Event: time 1715601393.060013, type 3 (EV_ABS), code 55 (ABS_MT_TOOL_TYPE), value 2
Event: time 1715601393.060013, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 1368
Event: time 1715601393.060013, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 2156
Event: time 1715601393.060013, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 12
Event: time 1715601393.060013, type 1 (EV_KEY), code 330 (BTN_TOUCH), value 1
Event: time 1715601393.060013, -------------- SYN_REPORT ------------
Event: time 1715601393.072162, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 42
Event: time 1715601393.072162, -------------- SYN_REPORT ------------
Event: time 1715601393.171312, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 12
Event: time 1715601393.171312, -------------- SYN_REPORT ------------
Event: time 1715601397.257059, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value -1
Event: time 1715601397.257059, type 1 (EV_KEY), code 330 (BTN_TOUCH), value 0
Event: time 1715601397.257059, -------------- SYN_REPORT ------------
)") << QList<TouchTestEvent>{};

    // A single finger is pressed, and transitions to palm
    QTest::addRow("ferrari-finger-to-palm-press-release")
        << TouchStreamBuilder::fromEvtestOutput(
               R"(
Event: time 1715601393.060013, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value 0
Event: time 1715601393.060013, type 3 (EV_ABS), code 55 (ABS_MT_TOOL_TYPE), value 1
Event: time 1715601393.060013, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 1368
Event: time 1715601393.060013, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 2156
Event: time 1715601393.060013, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 12
Event: time 1715601393.060013, type 1 (EV_KEY), code 330 (BTN_TOUCH), value 1
Event: time 1715601393.060013, -------------- SYN_REPORT ------------
Event: time 1715601393.072162, type 3 (EV_ABS), code 55 (ABS_MT_TOOL_TYPE), value 2
Event: time 1715601393.072162, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 42
Event: time 1715601393.072162, -------------- SYN_REPORT ------------
Event: time 1715601393.171312, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 12
Event: time 1715601393.171312, -------------- SYN_REPORT ------------
Event: time 1715601397.257059, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value -1
Event: time 1715601397.257059, type 1 (EV_KEY), code 330 (BTN_TOUCH), value 0
Event: time 1715601397.257059, -------------- SYN_REPORT ------------
)")
        << QList<TouchTestEvent>{TouchTestEvent{TouchPointBuilder()
                                                    .id(0)
                                                    .area({1068.43, 1639.01, 9.26471, 9.26471})
                                                    .normalPosition({0.662791, 0.761299})
                                                    .state(QEventPoint::State::Pressed)
                                                    .appendRawPosition({1368, 2156})
                                                    .pressure(1.0)
                                                    .build()},
                                 TouchTestEvent::cancel()};

    // Single point press
    // Then palm press and release, while first point remains in contact.
    QTest::addRow("ferrari-finger-and-palm-press-release")
        << TouchStreamBuilder::fromEvtestOutput(
               R"(
Event: time 1715672926.094343, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value 3311
Event: time 1715672926.094343, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 563
Event: time 1715672926.094343, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 690
Event: time 1715672926.094343, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 12
Event: time 1715672926.094343, type 1 (EV_KEY), code 330 (BTN_TOUCH), value 1
Event: time 1715672926.094343, -------------- SYN_REPORT ------------
Event: time 1715672927.460751, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 16
Event: time 1715672927.460751, -------------- SYN_REPORT ------------
Event: time 1715672928.513793, type 3 (EV_ABS), code 47 (ABS_MT_SLOT), value 1
Event: time 1715672928.513793, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value 3312
Event: time 1715672928.513793, type 3 (EV_ABS), code 55 (ABS_MT_TOOL_TYPE), value 2
Event: time 1715672928.513793, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 1488
Event: time 1715672928.513793, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 1465
Event: time 1715672928.513793, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 21
Event: time 1715672929.736110, -------------- SYN_REPORT ------------
Event: time 1715672929.748269, type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value -1
Event: time 1715672929.748269, -------------- SYN_REPORT ------------
Event: time 1715672929.970655, type 3 (EV_ABS), code 47 (ABS_MT_SLOT), value 0
Event: time 1715672929.970655, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 565
Event: time 1715672929.970655, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 696
Event: time 1715672929.970655, type 3 (EV_ABS), code 48 (ABS_MT_TOUCH_MAJOR), value 12
Event: time 1715672932.423703, -------------- SYN_REPORT ------------
Event: time 1715672945.970655, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 565
Event: time 1715672945.970655, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 696
Event: time 1715672945.423703, -------------- SYN_REPORT ------------
)")
        << QList<TouchTestEvent>{
               TouchTestEvent{TouchPointBuilder()
                                  .id(3311)
                                  .area({436.984, 521.395, 9.26471, 9.26471})
                                  .normalPosition({0.272771, 0.243644})
                                  .state(QEventPoint::State::Pressed)
                                  .appendRawPosition({563, 690})
                                  .pressure(1.0)
                                  .build()},
               TouchTestEvent::cancel(),
               TouchTestEvent{TouchPointBuilder()
                                  .id(3311)
                                  .area({438.553, 525.969, 9.26471, 9.26471})
                                  .normalPosition({0.272771, 0.243644})
                                  .state(QEventPoint::State::Pressed)
                                  .appendRawPosition({565, 696})
                                  .pressure(1.0)
                                  .build()},
               TouchTestEvent{TouchPointBuilder()
                                  .id(3311)
                                  .area({438.553, 525.969, 9.26471, 9.26471})
                                  .normalPosition({0.272771, 0.243644})
                                  .state(QEventPoint::State::Updated)
                                  .appendRawPosition({565, 696})
                                  .pressure(1.0)
                                  .build()},
           };
}

void TouchTest::test()
{
    QFETCH(QList<input_event>, inputEvents);
    QFETCH(QList<TouchTestEvent>, touchEvents);

    QList<TouchTestEvent> actualEvents;
    EpaperEvdevTouchScreenData data(QStringList{});

    // Copied from Ferrari
    data.hw_range_x_min = 0;
    data.hw_range_x_max = 2064;
    data.hw_range_y_min = 0;
    data.hw_range_y_max = 2832;
    data.hw_pressure_min = 0;
    data.hw_pressure_max = 0;
    data.m_rotate = QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1);
    data.m_screenGeometry = QRect(0, 0, 1620, 2160);

    connect(&data,
            &EpaperEvdevTouchScreenData::pointsChanged,
            &data,
            [&actualEvents](const QList<QWindowSystemInterface::TouchPoint>& points) {
                actualEvents.append(TouchTestEvent(points));
            });
    connect(&data, &EpaperEvdevTouchScreenData::cancelTouch, &data, [&actualEvents]() {
        actualEvents.append(TouchTestEvent::cancel());
    });

    for (const auto& event : inputEvents) {
        data.processInputEvent(&event);
    }

    QCOMPARE(actualEvents.size(), touchEvents.size());

    for (int i = 0; i < touchEvents.size(); ++i) {
        const auto& expected = touchEvents.at(i);
        const auto& actual = actualEvents.at(i);

        if (expected.isTouch() != actual.isTouch()) {
            qWarning() << "expected touch/cancel and got the opposite at pos" << i;
            qWarning() << "- expected touch" << expected.isTouch();
            qWarning() << "- actual touch" << actual.isTouch();
            QVERIFY(false);
        }

        if (expected.isCancel()) {
            // No data to verify here.
            continue;
        }

        if (expected.points().size() != actual.points().size()) {
            qWarning() << "unexpected point count at pos" << i;
            qWarning() << " - expected" << expected.points();
            qWarning() << " - actual" << actual.points();
            QVERIFY(false);
        }

        for (int j = 0; j < expected.points().size(); ++j) {
            const auto& actualP = actual.points().at(j);
            const auto& expectedP = expected.points().at(j);
            const auto& compare = pointsEqualOrError(expectedP, actualP);
            if (compare.has_value()) {
                qWarning() << "error at pos" << i << " point " << j;
                const auto dumpPoint = [](const auto& p) {
                    qWarning().nospace() << "TouchPoint(id: " << p.id << ", uniqueId: " << p.uniqueId
                                         << ", normalPosition: " << p.normalPosition << ", area: " << p.area
                                         << ", pressure: " << p.pressure << ", rotation: " << p.rotation
                                         << ", state: " << p.state << ", velocity: " << p.velocity
                                         << ", rawPositions: " << p.rawPositions << ")";
                };
                qWarning() << *compare;
                qWarning() << "expected:";
                dumpPoint(expectedP);
                qWarning() << "actual:";
                dumpPoint(actualP);
                QVERIFY(false);
            }
        }
    }
}

#undef QTEST_MAIN

// Like xochitl, we want to be able to run tests in CI, which don't have a decent QPA setup...
#define QTEST_MAIN(TestObject)                                                                                         \
    int main(int argc, char* argv[])                                                                                   \
    {                                                                                                                  \
        qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts");                                                            \
        QGuiApplication app(argc, argv);                                                                               \
        TestObject tc;                                                                                                 \
        QTEST_SET_MAIN_SOURCE_PATH;                                                                                    \
        QStringList args = app.arguments();                                                                            \
        return QTest::qExec(&tc, args);                                                                                \
    }

QTEST_MAIN(TouchTest)

#include "tst_touch.moc"
