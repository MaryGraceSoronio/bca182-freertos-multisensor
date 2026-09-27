/**
 * display.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Display subsystem (section 26): the SSD1306 OLED is owned exclusively by
 * DisplayTask.  No other task talks to the I2C bus, so no extra locking is
 * required around the display - the ownership rule itself is the
 * synchronisation.
 *
 * Pin map (see README "Pin Configuration"):
 *   PB6 - I2C1_SCL -> OLED SCL
 *   PB7 - I2C1_SDA -> OLED SDA
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>

#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up I2C1 and the SSD1306 controller; part of hardware init. */
void display_init(void);

/** True once the panel has answered its initialisation sequence. */
bool display_is_ready(void);

/** FreeRTOS task: renders the sample taken from displayQueue (section 27). */
void DisplayTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
