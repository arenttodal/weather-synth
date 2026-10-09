#include "Platform.h"
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace atmos::gui
{
bool osPrefersReducedMotion()
{
#if defined(_WIN32)
    BOOL animations = TRUE;
    if (SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) return animations == FALSE;
    return false;
#else
    return false;
#endif
}
} // namespace atmos::gui
