#include "Platform.h"
#import <AppKit/AppKit.h>

namespace atmos::gui
{
bool osPrefersReducedMotion()
{
    if (@available (macOS 10.12, *))
        return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
    return false;
}
} // namespace atmos::gui
