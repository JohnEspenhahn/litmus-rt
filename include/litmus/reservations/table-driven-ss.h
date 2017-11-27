#ifndef LITMUS_RESERVATIONS_TABLE_DRIVEN_SS_H
#define LITMUS_RESERVATIONS_TABLE_DRIVEN_SS_H

#include <litmus/reservations/reservation.h>
#include <litmus/reservations/table-driven.h>

void table_driven_reservation_ss_init(
	struct table_driven_reservation *tdres,
	lt_t major_cycle, struct lt_interval *intervals, unsigned int num_intervals);

#endif
