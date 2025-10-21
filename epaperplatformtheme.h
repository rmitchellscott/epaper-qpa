#pragma once
#include <qpa/qplatformtheme.h>

class EpaperPlatformTheme : public QPlatformTheme
{
public:
    QVariant themeHint(ThemeHint hint) const override;
};
