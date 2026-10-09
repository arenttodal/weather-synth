#pragma once

namespace atmos::gui
{
// True when the operating system asks apps to reduce motion (macOS: Accessibility > Display >
// Reduce motion; Windows: "Show animations in Windows" off). Linux: always false.
bool osPrefersReducedMotion();
} // namespace atmos::gui
