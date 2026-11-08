/*
 * Author: Raguram Kasthurisamy
 */
#ifndef SENSORS_H_
#define SENSORS_H_

#include <stdbool.h>

/* Checks which sensors are present and arms the accelerometer's data-ready trigger. */
int sensors_init(void);
/* Starts the sampling thread (once). */
void sensors_start(void);
/* Wakes the sampling thread for an immediate report. */
void sensors_request_sample(void);
bool sensors_env_ready(void);
bool sensors_accel_ready(void);

#endif /* SENSORS_H_ */
