#pragma once

// Flash writes suspend the cache used by the RGB bounce-buffer path.
// Hide the transient scanout, then restart RGB before exposing it again.
void screen_before_flash();
void screen_after_flash();
