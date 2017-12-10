#ifndef LITMUS_RESERVATIONS_CORE_H
#define LITMUS_RESERVATIONS_CORE_H

#include <litmus/rt_param.h>
#include <litmus/reservations/reservation.h>

void env_scheduler_update_after(
	struct reservation_environment* env,
	lt_t timeout);

#endif