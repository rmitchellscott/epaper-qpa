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

void TouchTest::test_data()
{
    QTest::addColumn<QList<input_event>>("events");
    QTest::addColumn<QList<QList<QWindowSystemInterface::TouchPoint>>>("points");

    QTest::addRow("no-events") << TouchStreamBuilder().build() << QList<QList<QWindowSystemInterface::TouchPoint>>{};

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
        << QList<QList<QWindowSystemInterface::TouchPoint>>{{TouchPointBuilder()
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
}

void TouchTest::test()
{
    QFETCH(QList<input_event>, events);
    QFETCH(QList<QList<QWindowSystemInterface::TouchPoint>>, points);

    QList<QList<QWindowSystemInterface::TouchPoint>> capturedPoints;
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

    connect(
        &data,
        &EpaperEvdevTouchScreenData::pointsChanged,
        &data,
        [&capturedPoints](const QList<QWindowSystemInterface::TouchPoint>& points) { capturedPoints.append(points); });

    for (const auto& event : events) {
        data.processInputEvent(&event);
    }

    QCOMPARE(capturedPoints.size(), points.size());

    for (int i = 0; i < points.size(); ++i) {
        const auto& expected = points.at(i);
        const auto& actual = capturedPoints.at(i);

        if (expected.size() != actual.size()) {
            qWarning() << "unexpected point count at pos" << i;
            qWarning() << " - expected" << expected;
            qWarning() << " - actual" << actual;
            QVERIFY(false);
        }

        for (int j = 0; j < expected.size(); ++j) {
            const auto& actualP = actual.at(j);
            const auto& expectedP = expected.at(j);
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
