#include "eco_stats.h"
#include "ai_model.h"
#include "ui/screens.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

// ---- Tunables ---------------------------------------------------------------

#define ECO_NVS_NAMESPACE   "storage"     // shared app namespace (see can.c)
#define ECO_NVS_KEY_BLOB    "eco_stats"
#define ECO_NVS_KEY_PRICE   "fuel_price"  // u16, grosze per liter
#define ECO_STATS_VERSION   1

// Lifetime distance required in BOTH the eco and normal buckets before the
// per-style l/100km baselines are considered calibrated. Below this the page
// shows calibration progress instead of made-up money.
#define ECO_CAL_MIN_DIST_M  5000.0f

#define ECO_PRICE_DEFAULT_GR 620   // 6.20 PLN/l
#define ECO_PRICE_STEP_GR    10
#define ECO_PRICE_MIN_GR     300
#define ECO_PRICE_MAX_GR     1500

// Petrol: 2.32 kg CO2 per liter burned (diesel would be 2.64).
#define ECO_CO2_G_PER_L     2320.0f

// Wear-event thresholds (accel derived from CAN speed, m/s^2) with hysteresis
// so one long braking maneuver counts as one event.
#define ECO_HARD_BRAKE_MS2          -3.0f
#define ECO_HARD_BRAKE_RELEASE_MS2  -1.5f
#define ECO_HARSH_ACCEL_MS2          2.5f
#define ECO_HARSH_ACCEL_RELEASE_MS2  1.25f
#define ECO_HIGH_RPM                 3000

// Cost model for "maintenance saved" (grosze). Documented assumptions, not
// measurements: pads+discs ~1200 PLN per 60k km with hard braking as the
// dominant wear driver; tires ~2000 PLN per 45k km; high-RPM minutes priced
// against oil/drivetrain service intervals.
#define ECO_COST_HARD_BRAKE_GR      15   // per avoided hard-brake event
#define ECO_COST_HARSH_ACCEL_GR     10   // per avoided harsh-accel event
#define ECO_COST_HIGHRPM_GR_PER_MIN  5   // per avoided high-RPM minute

#define ECO_NVS_SAVE_PERIOD_S 60.0f

#define ECO_COLOR_ECO    0x39B73E  // matches AI_TAB_COLOR_ECO_MAX
#define ECO_COLOR_AGGR   0xCB3328  // matches AI_TAB_COLOR_AGGRESSIVE_MAX
#define ECO_COLOR_NORMAL 0x9E9E9E
#define ECO_COLOR_TEXT   0x505050

// ---- Accumulators -----------------------------------------------------------

// Buckets indexed by ai_driving_style_t: [0]=normal [1]=eco [2]=aggressive.
typedef struct {
    uint32_t version;
    float    dist_m[3];
    float    fuel_ml[3];
    float    time_s[3];
    float    idle_fuel_ml;      // ignition on but not moving/classified
    uint32_t hard_brakes[3];
    uint32_t harsh_accels[3];
    float    high_rpm_s[3];
    uint32_t trees_harvested;
    uint32_t trees_withered;
    uint32_t streak_best;       // longest run of harvests without a wither
    uint32_t streak_cur;
} eco_stats_data_t;

static eco_stats_data_t s_life;   // persisted in NVS
static eco_stats_data_t s_trip;   // since boot (device sleeps between drives)

// Written by the sampling task, read by the LVGL task.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// Sampling state (sampling task only)
static uint32_t s_last_ts       = 0;
static float    s_last_spd_ms   = 0.0f;
static float    s_last_fuel_ml  = 0.0f;
static bool     s_fuel_anchored = false;
static bool     s_brake_latched = false;
static bool     s_accel_latched = false;
static uint8_t  s_last_ignition = 0;
static float    s_save_timer_s  = 0.0f;
static volatile bool s_dirty    = false;

static uint16_t s_price_gr = ECO_PRICE_DEFAULT_GR;

static const char *TAG = "eco_stats";

// ---- Derived view -----------------------------------------------------------

typedef struct {
    bool     calibrated;
    float    cal_eco_m;         // lifetime calibration progress (m)
    float    cal_normal_m;
    float    saved_fuel_l;
    int32_t  fuel_gr;           // fuel savings in grosze
    int32_t  wear_gr;           // maintenance savings in grosze (model)
    float    co2_kg;
    float    share[3];          // time share per style, 0..1
    float    dist_km;           // total classified distance in scope
    uint32_t harvested, withered, streak_best;
    float    trees_per_100km;
} eco_stats_view_t;

static float safe_rate(float num, float den)
{
    return (den > 0.0f) ? (num / den) : 0.0f;
}

// Rates (l/100km, events/m) always come from lifetime accumulators — the
// calibrated model — and are applied to the eco distance of the chosen scope.
static void eco_stats_compute_view(bool lifetime, eco_stats_view_t *v)
{
    eco_stats_data_t life, scope;

    portENTER_CRITICAL(&s_lock);
    life = s_life;
    scope = lifetime ? s_life : s_trip;
    portEXIT_CRITICAL(&s_lock);

    memset(v, 0, sizeof(*v));
    v->cal_eco_m    = life.dist_m[AI_DRIVING_STYLE_ECO];
    v->cal_normal_m = life.dist_m[AI_DRIVING_STYLE_NORMAL];
    v->calibrated   = v->cal_eco_m >= ECO_CAL_MIN_DIST_M &&
                      v->cal_normal_m >= ECO_CAL_MIN_DIST_M;

    float time_total = scope.time_s[0] + scope.time_s[1] + scope.time_s[2];
    for (int i = 0; i < 3; i++) v->share[i] = safe_rate(scope.time_s[i], time_total);

    float dist_total = scope.dist_m[0] + scope.dist_m[1] + scope.dist_m[2];
    v->dist_km = dist_total / 1000.0f;

    v->harvested   = scope.trees_harvested;
    v->withered    = scope.trees_withered;
    v->streak_best = life.streak_best;  // streak only makes sense lifetime
    v->trees_per_100km = safe_rate((float)scope.trees_harvested, dist_total / 100000.0f);

    if (!v->calibrated) return;

    float eco_dist_m = scope.dist_m[AI_DRIVING_STYLE_ECO];

    // Fuel: liters per 100 km = fuel_ml * 100 / dist_m
    float l100_eco    = safe_rate(life.fuel_ml[AI_DRIVING_STYLE_ECO] * 100.0f,
                                  life.dist_m[AI_DRIVING_STYLE_ECO]);
    float l100_normal = safe_rate(life.fuel_ml[AI_DRIVING_STYLE_NORMAL] * 100.0f,
                                  life.dist_m[AI_DRIVING_STYLE_NORMAL]);
    float dl100 = l100_normal - l100_eco;
    if (dl100 < 0.0f) dl100 = 0.0f;
    v->saved_fuel_l = (eco_dist_m / 100000.0f) * dl100;
    v->fuel_gr = (int32_t)(v->saved_fuel_l * (float)s_price_gr);
    v->co2_kg  = v->saved_fuel_l * ECO_CO2_G_PER_L / 1000.0f;

    // Maintenance: avoided events = eco distance x (non-eco rate - eco rate),
    // rates per meter from lifetime data. Clamped at zero per component.
    float noneco_dist = life.dist_m[AI_DRIVING_STYLE_NORMAL] +
                        life.dist_m[AI_DRIVING_STYLE_AGGRESSIVE];
    float brakes_noneco = (float)(life.hard_brakes[AI_DRIVING_STYLE_NORMAL] +
                                  life.hard_brakes[AI_DRIVING_STYLE_AGGRESSIVE]);
    float accels_noneco = (float)(life.harsh_accels[AI_DRIVING_STYLE_NORMAL] +
                                  life.harsh_accels[AI_DRIVING_STYLE_AGGRESSIVE]);
    float rpm_noneco    = life.high_rpm_s[AI_DRIVING_STYLE_NORMAL] +
                          life.high_rpm_s[AI_DRIVING_STYLE_AGGRESSIVE];

    float d_brakes = safe_rate(brakes_noneco, noneco_dist) -
                     safe_rate((float)life.hard_brakes[AI_DRIVING_STYLE_ECO],
                               life.dist_m[AI_DRIVING_STYLE_ECO]);
    float d_accels = safe_rate(accels_noneco, noneco_dist) -
                     safe_rate((float)life.harsh_accels[AI_DRIVING_STYLE_ECO],
                               life.dist_m[AI_DRIVING_STYLE_ECO]);
    float d_rpm_s  = safe_rate(rpm_noneco, noneco_dist) -
                     safe_rate(life.high_rpm_s[AI_DRIVING_STYLE_ECO],
                               life.dist_m[AI_DRIVING_STYLE_ECO]);
    if (d_brakes < 0.0f) d_brakes = 0.0f;
    if (d_accels < 0.0f) d_accels = 0.0f;
    if (d_rpm_s  < 0.0f) d_rpm_s  = 0.0f;

    v->wear_gr = (int32_t)(eco_dist_m * d_brakes * ECO_COST_HARD_BRAKE_GR +
                           eco_dist_m * d_accels * ECO_COST_HARSH_ACCEL_GR +
                           eco_dist_m * d_rpm_s / 60.0f * ECO_COST_HIGHRPM_GR_PER_MIN);
}

// ---- NVS ----------------------------------------------------------------------

static void eco_stats_nvs_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(ECO_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;

    eco_stats_data_t blob;
    size_t len = sizeof(blob);
    if (nvs_get_blob(handle, ECO_NVS_KEY_BLOB, &blob, &len) == ESP_OK &&
        len == sizeof(blob) && blob.version == ECO_STATS_VERSION) {
        s_life = blob;
    }
    uint16_t price;
    if (nvs_get_u16(handle, ECO_NVS_KEY_PRICE, &price) == ESP_OK &&
        price >= ECO_PRICE_MIN_GR && price <= ECO_PRICE_MAX_GR) {
        s_price_gr = price;
    }
    nvs_close(handle);
}

void eco_stats_flush(void)
{
    eco_stats_data_t copy;
    portENTER_CRITICAL(&s_lock);
    copy = s_life;
    s_dirty = false;
    portEXIT_CRITICAL(&s_lock);

    nvs_handle_t handle;
    if (nvs_open(ECO_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for stats save");
        return;
    }
    if (nvs_set_blob(handle, ECO_NVS_KEY_BLOB, &copy, sizeof(copy)) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

void eco_stats_reset_all(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t version = s_life.version;
    memset(&s_life, 0, sizeof(s_life));
    memset(&s_trip, 0, sizeof(s_trip));
    s_life.version = version;
    s_trip.version = version;
    portEXIT_CRITICAL(&s_lock);

    eco_stats_flush();
    eco_stats_update_ui_locked();
}

static void eco_stats_save_price(void)
{
    nvs_handle_t handle;
    if (nvs_open(ECO_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_u16(handle, ECO_NVS_KEY_PRICE, s_price_gr) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

// ---- Sampling -----------------------------------------------------------------

void eco_stats_init(void)
{
    memset(&s_life, 0, sizeof(s_life));
    memset(&s_trip, 0, sizeof(s_trip));
    s_life.version = ECO_STATS_VERSION;
    s_trip.version = ECO_STATS_VERSION;
    eco_stats_nvs_load();
}

void eco_stats_on_tree_harvested(void)
{
    portENTER_CRITICAL(&s_lock);
    s_life.trees_harvested++;
    s_trip.trees_harvested++;
    s_life.streak_cur++;
    if (s_life.streak_cur > s_life.streak_best) s_life.streak_best = s_life.streak_cur;
    portEXIT_CRITICAL(&s_lock);
    s_dirty = true;
}

void eco_stats_on_tree_withered(void)
{
    portENTER_CRITICAL(&s_lock);
    s_life.trees_withered++;
    s_trip.trees_withered++;
    s_life.streak_cur = 0;
    portEXIT_CRITICAL(&s_lock);
    s_dirty = true;
}

void eco_stats_process_sample(const can_data_t *data)
{
    if (data == NULL) return;

    // The logger task re-reads the same snapshot when the bus is quiet; only a
    // fresh CAN timestamp carries new information. Backward jump = re-anchor.
    if (data->timestamp == s_last_ts) return;
    if (data->timestamp < s_last_ts) {
        s_last_ts = data->timestamp;
        s_fuel_anchored = false;
        return;
    }

    float real_dt = 0.1f;
    if (s_last_ts > 0) {
        real_dt = (float)(data->timestamp - s_last_ts) / 1000.0f;
        if (real_dt < 0.01f) real_dt = 0.01f;
    }
    s_last_ts = data->timestamp;
    bool gap = real_dt > 2.0f;           // bus silence: don't fake dynamics across it
    float dt_s = (real_dt > 1.0f) ? 1.0f : real_dt;  // nor attribute a big blob

    float spd_ms = (float)data->speed / 3.6f;
    float accel  = gap ? 0.0f : (spd_ms - s_last_spd_ms) / dt_s;
    s_last_spd_ms = spd_ms;

    // Fuel counter is cumulative ml and user-resettable; a negative delta or an
    // implausible spike (>50 ml per sample = 1800 l/h) means reset/glitch.
    float dfuel = 0.0f;
    if (s_fuel_anchored) {
        dfuel = data->fuel_consumption - s_last_fuel_ml;
        if (dfuel < 0.0f || dfuel > 50.0f) dfuel = 0.0f;
    }
    s_last_fuel_ml = data->fuel_consumption;
    s_fuel_anchored = true;

    ai_model_snapshot_t snap;
    ai_model_get_snapshot(&snap);

    // Same gate as the training-data filters in ai_model_process_sample().
    bool classified = data->ignition && !data->rear_gear && data->speed >= 2 &&
                      snap.ready && snap.style <= AI_DRIVING_STYLE_AGGRESSIVE;

    portENTER_CRITICAL(&s_lock);
    if (classified) {
        int b = (int)snap.style;
        float dm = spd_ms * dt_s;
        s_life.dist_m[b] += dm;           s_trip.dist_m[b] += dm;
        s_life.time_s[b] += dt_s;         s_trip.time_s[b] += dt_s;
        s_life.fuel_ml[b] += dfuel;       s_trip.fuel_ml[b] += dfuel;
        if (data->rpm > ECO_HIGH_RPM) {
            s_life.high_rpm_s[b] += dt_s; s_trip.high_rpm_s[b] += dt_s;
        }

        if (!s_brake_latched && accel <= ECO_HARD_BRAKE_MS2) {
            s_brake_latched = true;
            s_life.hard_brakes[b]++;      s_trip.hard_brakes[b]++;
        } else if (s_brake_latched && accel >= ECO_HARD_BRAKE_RELEASE_MS2) {
            s_brake_latched = false;
        }
        if (!s_accel_latched && accel >= ECO_HARSH_ACCEL_MS2) {
            s_accel_latched = true;
            s_life.harsh_accels[b]++;     s_trip.harsh_accels[b]++;
        } else if (s_accel_latched && accel <= ECO_HARSH_ACCEL_RELEASE_MS2) {
            s_accel_latched = false;
        }
        s_dirty = true;
    } else {
        if (data->ignition && dfuel > 0.0f) {
            s_life.idle_fuel_ml += dfuel; s_trip.idle_fuel_ml += dfuel;
            s_dirty = true;
        }
        s_brake_latched = false;
        s_accel_latched = false;
    }
    portEXIT_CRITICAL(&s_lock);

    // Persist on ignition-off and periodically while dirty (flash-friendly).
    bool ignition_off_edge = (s_last_ignition && !data->ignition);
    s_last_ignition = data->ignition;
    s_save_timer_s += dt_s;
    if ((ignition_off_edge || s_save_timer_s >= ECO_NVS_SAVE_PERIOD_S) && s_dirty) {
        s_save_timer_s = 0.0f;
        eco_stats_flush();
    }
}

// ---- UI -------------------------------------------------------------------------

static bool s_show_lifetime = false;   // default scope: this trip

static lv_obj_t *s_page          = NULL;
static lv_obj_t *s_btn_trip      = NULL;
static lv_obj_t *s_btn_total     = NULL;
static lv_obj_t *s_lbl_money     = NULL;
static lv_obj_t *s_lbl_money_sub = NULL;
static lv_obj_t *s_bar_cal       = NULL;
static lv_obj_t *s_lbl_fuel      = NULL;
static lv_obj_t *s_lbl_fuel_sub  = NULL;
static lv_obj_t *s_lbl_wear      = NULL;
static lv_obj_t *s_lbl_co2       = NULL;
static lv_obj_t *s_lbl_share     = NULL;
static lv_obj_t *s_seg[3]        = {NULL, NULL, NULL};
static lv_obj_t *s_lbl_orchard   = NULL;
static lv_obj_t *s_lbl_price     = NULL;

static void scope_button_refresh(void)
{
    lv_obj_set_style_bg_color(s_btn_trip,
        lv_color_hex(s_show_lifetime ? 0xffdddddd : 0xff333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lv_obj_get_child(s_btn_trip, 0),
        lv_color_hex(s_show_lifetime ? ECO_COLOR_TEXT : 0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_total,
        lv_color_hex(s_show_lifetime ? 0xff333333 : 0xffdddddd), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lv_obj_get_child(s_btn_total, 0),
        lv_color_hex(s_show_lifetime ? 0xffffffff : ECO_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void scope_button_cb(lv_event_t *e)
{
    s_show_lifetime = (lv_event_get_target(e) == s_btn_total);
    scope_button_refresh();
    eco_stats_update_ui_locked();
}

static void price_button_cb(lv_event_t *e)
{
    int step = (lv_event_get_user_data(e) != NULL) ? ECO_PRICE_STEP_GR : -ECO_PRICE_STEP_GR;
    int price = (int)s_price_gr + step;
    if (price < ECO_PRICE_MIN_GR) price = ECO_PRICE_MIN_GR;
    if (price > ECO_PRICE_MAX_GR) price = ECO_PRICE_MAX_GR;
    s_price_gr = (uint16_t)price;
    eco_stats_save_price();
    eco_stats_update_ui_locked();
}

static lv_obj_t *make_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "");
    return lbl;
}

static lv_obj_t *make_scope_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 92, 34);
    lv_obj_set_style_radius(btn, 17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, scope_button_cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

void eco_stats_create_page(void)
{
    if (objects.tabview_main == NULL || s_page != NULL) return;

    lv_obj_t *tab = lv_tabview_add_tab(objects.tabview_main, "STATS");
    // Appended last; reorder to sit right after the AI page. Navigation (full
    // screen gestures + dots, see ui_pager_init) is purely child-order based,
    // and the tab button bar is hidden, so the stale btnmatrix map is harmless.
    lv_obj_move_to_index(tab, 1);
    // screens.c selects startup page 0 (AI) against the old order; re-point
    // (AI's own index doesn't shift, but re-asserting keeps this in sync
    // with whatever startup tab screens.c picks).
    lv_tabview_set_act(objects.tabview_main, 0, LV_ANIM_OFF);
    s_page = tab;

    // Root container mirrors the SAVE page background (grey wash, flex column).
    lv_obj_t *root = lv_obj_create(tab);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0xffbfbfbf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(root, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(root, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(root, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(root, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(root, 30, LV_PART_MAIN | LV_STATE_DEFAULT);  // above pager dots
    lv_obj_set_style_pad_row(root, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Header: title left, TRIP/TOTAL scope toggle right.
    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = make_label(header, &lv_font_montserrat_20, ECO_COLOR_TEXT);
    lv_label_set_text(title, "ECO SAVINGS");

    lv_obj_t *toggle = lv_obj_create(header);
    lv_obj_remove_style_all(toggle);
    lv_obj_set_size(toggle, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(toggle, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(toggle, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    s_btn_trip  = make_scope_button(toggle, "TRIP");
    s_btn_total = make_scope_button(toggle, "TOTAL");
    scope_button_refresh();

    // Hero card: the headline money figure (or calibration progress).
    lv_obj_t *hero = make_card(root);
    lv_obj_set_size(hero, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(hero, 1);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_lbl_money = make_label(hero, &lv_font_montserrat_48, ECO_COLOR_ECO);
    s_bar_cal = lv_bar_create(hero);
    lv_obj_set_size(s_bar_cal, 260, 10);
    lv_bar_set_range(s_bar_cal, 0, 100);
    lv_obj_set_style_bg_color(s_bar_cal, lv_color_hex(0xffdddddd), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_bar_cal, lv_color_hex(ECO_COLOR_ECO), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    s_lbl_money_sub = make_label(hero, &lv_font_montserrat_14, ECO_COLOR_TEXT);

    // Breakdown cards: fuel / maintenance / CO2.
    lv_obj_t *cards = lv_obj_create(root);
    lv_obj_remove_style_all(cards);
    lv_obj_set_size(cards, LV_PCT(100), 104);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cards, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cards, LV_OBJ_FLAG_SCROLLABLE);

    const char *card_titles[3] = {"FUEL SAVED", "MAINTENANCE (EST.)", "CO2 AVOIDED"};
    lv_obj_t **card_values[3]  = {&s_lbl_fuel, &s_lbl_wear, &s_lbl_co2};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *card = make_card(cards);
        lv_obj_set_height(card, LV_PCT(100));
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *t = make_label(card, &lv_font_montserrat_14, ECO_COLOR_TEXT);
        lv_label_set_text(t, card_titles[i]);
        *card_values[i] = make_label(card, &lv_font_montserrat_28, 0xff222222);
        if (i == 0) s_lbl_fuel_sub = make_label(card, &lv_font_montserrat_14, ECO_COLOR_TEXT);
    }

    // Driving style share: stacked horizontal bar + legend.
    lv_obj_t *share = make_card(root);
    lv_obj_set_size(share, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(share, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(share, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_lbl_share = make_label(share, &lv_font_montserrat_14, ECO_COLOR_TEXT);

    lv_obj_t *bar = lv_obj_create(share);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 16);
    lv_obj_set_style_radius(bar, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bar, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xffdddddd), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    const uint32_t seg_colors[3] = {ECO_COLOR_ECO, ECO_COLOR_NORMAL, ECO_COLOR_AGGR};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *seg = lv_obj_create(bar);
        lv_obj_remove_style_all(seg);
        lv_obj_set_size(seg, 0, LV_PCT(100));
        lv_obj_set_style_bg_color(seg, lv_color_hex(seg_colors[i]), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        s_seg[i] = seg;
    }

    // Orchard statistics.
    lv_obj_t *orchard = make_card(root);
    lv_obj_set_size(orchard, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(orchard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(orchard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(orchard, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *otitle = make_label(orchard, &lv_font_montserrat_14, ECO_COLOR_TEXT);
    lv_label_set_text(otitle, "ORCHARD");
    s_lbl_orchard = make_label(orchard, &lv_font_montserrat_16, 0xff222222);

    // Footer: fuel price stepper (feeds the money conversion).
    lv_obj_t *footer = lv_obj_create(root);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(footer, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_price = make_label(footer, &lv_font_montserrat_14, ECO_COLOR_TEXT);
    const char *sym[2] = {LV_SYMBOL_MINUS, LV_SYMBOL_PLUS};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_btn_create(footer);
        lv_obj_set_size(btn, 40, 30);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xff333333), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, sym[i]);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, price_button_cb, LV_EVENT_CLICKED, i ? (void *)1 : NULL);
    }

    eco_stats_update_ui_locked();
}

lv_obj_t *eco_stats_get_page(void)
{
    return s_page;
}

int32_t eco_stats_get_trip_saved_gr(bool *calibrated)
{
    eco_stats_view_t v;
    eco_stats_compute_view(false, &v);
    if (calibrated) *calibrated = v.calibrated;
    return v.calibrated ? (v.fuel_gr + v.wear_gr) : 0;
}

void eco_stats_update_ui_locked(void)
{
    if (s_page == NULL) return;

    eco_stats_view_t v;
    eco_stats_compute_view(s_show_lifetime, &v);

    if (v.calibrated) {
        int32_t total_gr = v.fuel_gr + v.wear_gr;
        lv_label_set_text_fmt(s_lbl_money, "%ld.%02ld PLN",
                              (long)(total_gr / 100), (long)(total_gr % 100));
        lv_label_set_text_fmt(s_lbl_money_sub, "saved with eco driving over %d.%d km",
                              (int)v.dist_km, (int)(v.dist_km * 10.0f) % 10);
        lv_obj_add_flag(s_bar_cal, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text_fmt(s_lbl_fuel, "%d.%02d L",
                              (int)v.saved_fuel_l, (int)(v.saved_fuel_l * 100.0f) % 100);
        lv_label_set_text_fmt(s_lbl_fuel_sub, "%ld.%02ld PLN",
                              (long)(v.fuel_gr / 100), (long)(v.fuel_gr % 100));
        lv_label_set_text_fmt(s_lbl_wear, "~%ld.%02ld PLN",
                              (long)(v.wear_gr / 100), (long)(v.wear_gr % 100));
        lv_label_set_text_fmt(s_lbl_co2, "%d.%d kg",
                              (int)v.co2_kg, (int)(v.co2_kg * 10.0f) % 10);
    } else {
        // Not enough per-style baseline data yet: show progress, no invented money.
        lv_label_set_text(s_lbl_money, "CALIBRATING");
        float p = 0.5f * ((v.cal_eco_m    > ECO_CAL_MIN_DIST_M ? ECO_CAL_MIN_DIST_M : v.cal_eco_m) +
                          (v.cal_normal_m > ECO_CAL_MIN_DIST_M ? ECO_CAL_MIN_DIST_M : v.cal_normal_m))
                  / ECO_CAL_MIN_DIST_M;
        lv_obj_clear_flag(s_bar_cal, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_bar_cal, (int32_t)(p * 100.0f), LV_ANIM_OFF);
        lv_label_set_text_fmt(s_lbl_money_sub,
                              "learning your car: %d.%d/%d km eco, %d.%d/%d km normal",
                              (int)(v.cal_eco_m / 1000.0f), (int)(v.cal_eco_m / 100.0f) % 10,
                              (int)(ECO_CAL_MIN_DIST_M / 1000.0f),
                              (int)(v.cal_normal_m / 1000.0f), (int)(v.cal_normal_m / 100.0f) % 10,
                              (int)(ECO_CAL_MIN_DIST_M / 1000.0f));
        lv_label_set_text(s_lbl_fuel, "--");
        lv_label_set_text(s_lbl_fuel_sub, "");
        lv_label_set_text(s_lbl_wear, "--");
        lv_label_set_text(s_lbl_co2, "--");
    }

    int pct_eco  = (int)(v.share[AI_DRIVING_STYLE_ECO] * 100.0f + 0.5f);
    int pct_aggr = (int)(v.share[AI_DRIVING_STYLE_AGGRESSIVE] * 100.0f + 0.5f);
    int pct_norm = 100 - pct_eco - pct_aggr;
    if (pct_norm < 0) pct_norm = 0;
    if (v.share[0] + v.share[1] + v.share[2] <= 0.0f) { pct_eco = pct_aggr = 0; pct_norm = 0; }
    // U+2022 bullet: the only separator glyph the built-in Montserrat fonts have
    lv_label_set_text_fmt(s_lbl_share, "STYLE SHARE    %d%% ECO  \xE2\x80\xA2  %d%% NORMAL  \xE2\x80\xA2  %d%% AGGRESSIVE",
                          pct_eco, pct_norm, pct_aggr);
    lv_obj_set_width(s_seg[0], LV_PCT(pct_eco));
    lv_obj_set_width(s_seg[1], LV_PCT(pct_norm));
    lv_obj_set_width(s_seg[2], LV_PCT(pct_aggr));

    char trees_buf[24];
    if (v.trees_per_100km > 0.0f) {
        snprintf(trees_buf, sizeof(trees_buf), "%d.%d trees/100 km",
                 (int)v.trees_per_100km, (int)(v.trees_per_100km * 10.0f) % 10);
    } else {
        snprintf(trees_buf, sizeof(trees_buf), "-- trees/100 km");
    }
    lv_label_set_text_fmt(s_lbl_orchard,
                          "%lu trees  \xE2\x80\xA2  %lu harvested  \xE2\x80\xA2  %lu withered  \xE2\x80\xA2  best streak %lu  \xE2\x80\xA2  %s",
                          (unsigned long)ai_model_get_orchard_count(),
                          (unsigned long)v.harvested, (unsigned long)v.withered,
                          (unsigned long)v.streak_best, trees_buf);

    lv_label_set_text_fmt(s_lbl_price, "FUEL PRICE  %u.%02u PLN/L",
                          s_price_gr / 100, s_price_gr % 100);
}
