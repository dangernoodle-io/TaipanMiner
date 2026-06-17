#pragma once

/*
 * JSON section and capability identifier strings for /api/info and /api/health.
 * Use these constants everywhere a section name or capability key is registered
 * or compared — never hardcode the strings directly.
 */

#define WEBUI_SECTION_MINING "mining"
#define WEBUI_SECTION_POOL   "pool"
#define WEBUI_SECTION_KNOT   "knot"

#define WEBUI_CAP_UI    "ui"
#define WEBUI_CAP_ASIC  "asic"
#define WEBUI_CAP_FAN   "fan"
#define WEBUI_CAP_POWER "power"
#define WEBUI_CAP_KNOT  "knot"
