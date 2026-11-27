#ifndef REIPAN_UI_LOCALE_H
#define REIPAN_UI_LOCALE_H

#include <string>
#include <string_view>
#include <utility>

namespace ReiPanUiLocale
{
#ifdef REIPAN_SLPS_25544_CHS
inline constexpr std::pair<std::string_view, std::string_view> kTranslations[] = {
#include "reipan_ui_zh_cn.inc"
};
#endif

constexpr std::string_view Translate(std::string_view text)
{
#ifdef REIPAN_SLPS_25544_CHS
    for (const auto &[original, translated] : kTranslations)
    {
        if (text == original)
            return translated;
    }
#endif
    return text;
}

consteval const char *UiText(const char *text)
{
    return Translate(text).data();
}

std::string ChineseFontPath();
std::string PortFontPath();
}

#endif
