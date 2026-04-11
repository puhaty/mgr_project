#ifndef AI_MODEL_H
#define AI_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "can.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	AI_DRIVING_STYLE_AGGRESSIVE = 0,
	AI_DRIVING_STYLE_NORMAL = 1,
	AI_DRIVING_STYLE_ECO = 2
} ai_driving_style_t;

typedef struct {
	int32_t score;
	ai_driving_style_t style;
	bool ready;
} ai_model_snapshot_t;

void ai_model_init(void);
void ai_model_process_sample(const can_data_t *data);
void ai_model_update_ui_locked(void);
void ai_model_get_snapshot(ai_model_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif // AI_MODEL_H