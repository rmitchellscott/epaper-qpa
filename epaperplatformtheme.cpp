#include "epaperplatformtheme.h"

#include <QVariant>

QVariant EpaperPlatformTheme::themeHint(ThemeHint hint) const
{
    bool ok = false;
    int value = 0;

    switch (hint) {
    case QPlatformTheme::StartDragDistance:
        value = qEnvironmentVariableIntValue("EPPLATFORMTHEME_STARTDRAGDISTANCE", &ok);
        if (!ok) {
            value = 30; // Qt used 10
        }
        return value;
    default:
        return QPlatformTheme::defaultThemeHint(hint);
    }
}
