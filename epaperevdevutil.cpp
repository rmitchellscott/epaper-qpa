#include "epaperevdevutil.h"

// This code is originally from QEvdevUtil, extracted to keep this code independent.
namespace EpaperEvdevUtil {

ParsedSpecification parseSpecification(const QString &specification)
{
    ParsedSpecification result;

    result.args = specification.split(QLatin1Char(':'));

    for (const QStringView &arg : qAsConst(result.args)) {
        if (arg.startsWith(QString("/dev/"))) {
            // if device is specified try to use it
            result.devices.append(arg.toString());
        } else {
            // build new specification without /dev/ elements
            result.spec += arg.toString() + QLatin1Char(':');
        }
    }

    if (!result.spec.isEmpty())
        result.spec.chop(1); // remove trailing ':'

    return result;
}

}

