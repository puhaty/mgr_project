#include "ai_model.h"
#include "eco_model.h"
#include "ui/screens.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define ML_WINDOW_SIZE 20

#define AI_SCORE_MIN 0
#define AI_SCORE_MAX 100
#define AI_SCORE_START 50
#define AI_SCORE_STEP 1

#define AI_METER_COLOR_ECO_MAX 0x2aff00
#define AI_METER_COLOR_AGGRESSIVE_MAX 0xff0000

static float s_throttle_history[ML_WINDOW_SIZE] = {0};
static int s_history_index = 0;
static int s_history_count = 0;
static uint32_t s_last_timestamp = 0;
static float s_last_speed_ms = 0.0f;

static volatile int32_t s_score = AI_SCORE_START;
static volatile ai_driving_style_t s_style = AI_DRIVING_STYLE_NORMAL;
static volatile bool s_ready = false;
static bool s_meter_neutral_color_initialized = false;
static lv_color_t s_meter_neutral_color;

static int16_t clamp_to_i16(float value)
{
	if (value > (float)INT16_MAX) {
		return INT16_MAX;
	}
	if (value < (float)INT16_MIN) {
		return INT16_MIN;
	}
	return (int16_t)lrintf(value);
}

static float compute_throttle_std(void)
{
	float sum = 0.0f;
	float variance = 0.0f;

	for (int i = 0; i < ML_WINDOW_SIZE; i++) {
		sum += s_throttle_history[i];
	}
	float mean = sum / (float)ML_WINDOW_SIZE;

	for (int i = 0; i < ML_WINDOW_SIZE; i++) {
		float diff = s_throttle_history[i] - mean;
		variance += diff * diff;
	}

	return sqrtf(variance / (float)ML_WINDOW_SIZE);
}

void ai_model_init(void)
{
	memset(s_throttle_history, 0, sizeof(s_throttle_history));
	s_history_index = 0;
	s_history_count = 0;
	s_last_timestamp = 0;
	s_last_speed_ms = 0.0f;

	s_score = AI_SCORE_START;
	s_style = AI_DRIVING_STYLE_NORMAL;
	s_ready = false;
}

void ai_model_process_sample(const can_data_t *data)
{
	if (data == NULL) {
		return;
	}

	if (!data->ignition) {
		s_history_index = 0;
		s_history_count = 0;
		s_last_timestamp = 0;
		s_last_speed_ms = 0.0f;
		s_style = AI_DRIVING_STYLE_NORMAL;
		s_ready = false;
		return;
	}

	float dt_s = 0.1f;
	if (s_last_timestamp > 0 && data->timestamp > s_last_timestamp) {
		dt_s = ((float)(data->timestamp - s_last_timestamp)) / 1000.0f;
		if (dt_s < 0.01f) {
			dt_s = 0.01f;
		}
	}
	s_last_timestamp = data->timestamp;

	float current_speed_ms = ((float)data->speed) / 3.6f;
	float acceleration = (current_speed_ms - s_last_speed_ms) / dt_s;
	s_last_speed_ms = current_speed_ms;

	s_throttle_history[s_history_index] = (float)data->throttle_pedal;
	s_history_index = (s_history_index + 1) % ML_WINDOW_SIZE;
	if (s_history_count < ML_WINDOW_SIZE) {
		s_history_count++;
	}

	if (s_history_count < ML_WINDOW_SIZE) {
		s_ready = false;
		return;
	}

	float throttle_std = compute_throttle_std();

	int16_t features[6];
	features[0] = (int16_t)data->speed;
	features[1] = clamp_to_i16((float)data->rpm);
	features[2] = (int16_t)data->throttle_pedal;
	features[3] = clamp_to_i16((float)data->brake_pedal);
	features[4] = clamp_to_i16(acceleration);
	features[5] = clamp_to_i16(throttle_std);

	int prediction = (int)eco_model_predict(features, 6);
	if (prediction < AI_DRIVING_STYLE_AGGRESSIVE || prediction > AI_DRIVING_STYLE_ECO) {
		return;
	}

	s_style = (ai_driving_style_t)prediction;
	s_ready = true;

	if (s_style == AI_DRIVING_STYLE_ECO && s_score < AI_SCORE_MAX) {
		s_score += AI_SCORE_STEP;
	} else if (s_style == AI_DRIVING_STYLE_AGGRESSIVE && s_score > AI_SCORE_MIN) {
		s_score -= AI_SCORE_STEP;
	}

	if (s_score < AI_SCORE_MIN) {
		s_score = AI_SCORE_MIN;
	}
	if (s_score > AI_SCORE_MAX) {
		s_score = AI_SCORE_MAX;
	}
}

void ai_model_get_snapshot(ai_model_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return;
	}

	out_snapshot->score = s_score;
	out_snapshot->style = s_style;
	out_snapshot->ready = s_ready;
}

void ai_model_update_ui_locked(void)
{
	ai_model_snapshot_t snapshot;
	ai_model_get_snapshot(&snapshot);

	if (objects.meter_score != NULL && screen_screen_main_state.indicator != NULL) {
		lv_meter_set_indicator_value(
			objects.meter_score,
			screen_screen_main_state.indicator,
			snapshot.score
		);

		if (!s_meter_neutral_color_initialized) {
			s_meter_neutral_color = lv_obj_get_style_bg_color(objects.meter_score, LV_PART_MAIN | LV_STATE_DEFAULT);
			s_meter_neutral_color_initialized = true;
		}

		int32_t score = snapshot.score;
		if (score < AI_SCORE_MIN) {
			score = AI_SCORE_MIN;
		} else if (score > AI_SCORE_MAX) {
			score = AI_SCORE_MAX;
		}

		lv_color_t target_color = s_meter_neutral_color;
		uint8_t mix = 0;

		if (score < AI_SCORE_START) {
			target_color = lv_color_hex(AI_METER_COLOR_AGGRESSIVE_MAX);
			mix = (uint8_t)(((AI_SCORE_START - score) * 255) / AI_SCORE_START);
		} else if (score > AI_SCORE_START) {
			target_color = lv_color_hex(AI_METER_COLOR_ECO_MAX);
			mix = (uint8_t)(((score - AI_SCORE_START) * 255) / (AI_SCORE_MAX - AI_SCORE_START));
		}

		lv_obj_set_style_bg_color(
			objects.meter_score,
			lv_color_mix(target_color, s_meter_neutral_color, mix),
			LV_PART_MAIN | LV_STATE_DEFAULT
		);
	}
}