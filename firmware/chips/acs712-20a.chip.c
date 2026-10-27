#include "wokwi-api.h"
#include <stddef.h>

typedef struct {
  pin_t pin_out;
  uint32_t current_attr;
} chip_state_t;

static chip_state_t chip;

static void update_output(void *user_data) {
  float current_a = attr_read_float(chip.current_attr);
  float output_v = 1.65f + (current_a * 0.100f);

  if (output_v < 0.0f) {
    output_v = 0.0f;
  }

  if (output_v > 3.3f) {
    output_v = 3.3f;
  }

  pin_dac_write(chip.pin_out, output_v);
}

void chip_init(void) {
  chip.pin_out = pin_init("OUT", ANALOG);

  chip.current_attr = attr_init_float(
    "acs71220a",
    0.0f
  );

  const timer_config_t config = {
    .callback = update_output,
    .user_data = NULL,
  };

  timer_t timer_id = timer_init(&config);

  timer_start(timer_id, 50000, true);

  update_output(NULL);
}
