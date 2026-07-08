#ifndef AI_MODEL_H
#define AI_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "can.h"

#ifdef __cplusplus
extern "C" {
#endif

// Order must match CLASS_ORDER in ML_training notebook: ["normal", "eco", "aggressive"]
typedef enum {
	AI_DRIVING_STYLE_NORMAL = 0,
	AI_DRIVING_STYLE_ECO = 1,
	AI_DRIVING_STYLE_AGGRESSIVE = 2
} ai_driving_style_t;

typedef struct {
	int32_t score;
	ai_driving_style_t style;
	bool ready;
} ai_model_snapshot_t;

void ai_model_init(void);
void ai_model_process_sample(const can_data_t *data);
void ai_model_reset_orchard(void);
uint32_t ai_model_get_orchard_count(void);
void ai_model_update_ui_locked(void);
void ai_model_get_snapshot(ai_model_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif // AI_MODEL_H