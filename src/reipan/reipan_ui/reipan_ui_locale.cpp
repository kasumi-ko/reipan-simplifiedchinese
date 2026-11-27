#include "reipan_ui_locale.h"
#include "reipan/io/reipan_paths.h"

#include <SDL3/SDL_stdinc.h>

namespace ReiPanUiLocale
{
std::string PortFontPath()
{
#ifdef REIPAN_SLPS_25544_CHS
    char path[1024];
    if (ReiPan_PathResolveAsset("resources/fonts/LXGWWenKaiGB-Medium.ttf", path, sizeof(path)))
        return path;
#endif
    return {};
}

std::string ChineseFontPath()
{
#if defined(REIPAN_SLPS_25544_CHS) && defined(_WIN32)
    if (const char *directory = SDL_getenv("SystemRoot"))
        return std::string(directory) + "/Fonts/msyh.ttc";
#endif
    return {};
}
}
