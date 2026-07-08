#ifndef ECO_STATS_H
#define ECO_STATS_H

#include "can.h"

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Savings/statistics page ("STATS", second pager page) plus the accumulators
// behind it. Fuel savings use a self-calibrating baseline: the device measures
// its own l/100km separately for eco/normal/aggressive windows (classified by
// ai_model) and prices the eco distance against the driver's own normal-style
// consumption. Maintenance savings use the same counterfactual logic on wear
// events (hard braking, harsh acceleration, high-RPM time) with an explicit
// cost model. Lifetime counters persist in NVS; trip counters reset at boot
// (the device deep-sleeps between drives, so boot == trip).

void eco_stats_init(void);

// Feed one 100 ms CAN snapshot. Call right after ai_model_process_sample()
// (same task) so the style classification for the sample is current.
void eco_stats_process_sample(const can_data_t *data);

// Orchard lifetime bookkeeping, called by ai_model on harvest/wither events.
void eco_stats_on_tree_harvested(void);
void eco_stats_on_tree_withered(void);

// Persist lifetime counters now (e.g. right before deep sleep).
void eco_stats_flush(void);

// Clear all lifetime + trip accumulators (fuel/wear savings, style time,
// orchard counters, streaks), persist the zeroed state and refresh the STATS
// page. Must run with the LVGL lock held (call from a UI action).
void eco_stats_reset_all(void);

// Build the STATS page and insert it as the second pager page. Must run with
// the LVGL lock held, after ui_init() and BEFORE ui_pager_init() so the dot
// indicator and full-bleed styling pick the page up.
void eco_stats_create_page(void);

// Refresh the page labels. Must run with the LVGL lock held.
void eco_stats_update_ui_locked(void);

// Money saved so far this trip (fuel + maintenance, grosze). *calibrated is
// set to false (return value 0) while the per-style baseline is still
// learning — see the module comment above. Cheap enough to poll every UI
// tick (e.g. from the AI/orchard page); does not touch any UI object.
int32_t eco_stats_get_trip_saved_gr(bool *calibrated);

// Tab page object of the STATS view (NULL before eco_stats_create_page()).
// Used by the pager to keep the FPS/CPU monitor off this page.
lv_obj_t *eco_stats_get_page(void);

#ifdef __cplusplus
}
#endif

#endif // ECO_STATS_H
