#include "theme_data.h"

extern const unsigned char ex_theme_dark[], ex_theme_light[], ex_theme_hc_dark[], ex_theme_hc_light[];

const char *ex_builtin_theme_text(ExBuiltinTheme which) {
    switch (which) {
    case EX_THEME_DARK: return (const char *)ex_theme_dark;
    case EX_THEME_LIGHT: return (const char *)ex_theme_light;
    case EX_THEME_HC_DARK: return (const char *)ex_theme_hc_dark;
    case EX_THEME_HC_LIGHT: return (const char *)ex_theme_hc_light;
    default: return 0;
    }
}
