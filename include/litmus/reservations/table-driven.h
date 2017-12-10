#ifndef LITMUS_RESERVATIONS_TABLE_DRIVEN_H
#define LITMUS_RESERVATIONS_TABLE_DRIVEN_H

#include <litmus/reservations/reservation.h>

struct table_driven_reservation {
	/* extend basic reservation */
	struct reservation res;

	lt_t major_cycle;
	unsigned int next_interval;
	unsigned int num_intervals;
	struct lt_interval *intervals;

	/* info about current scheduling slot */
	struct lt_interval cur_interval;
	lt_t major_cycle_start;

	/* used by slack stealing ONLY */
	lt_t expected_exec_cost;
	lt_t slack_consumed;
	lt_t running_slack;
	lt_t cur_slack;

	struct list_head *aperiodic_clients;
};

void table_driven_reservation_init(struct table_driven_reservation *tdres,
	lt_t major_cycle, struct lt_interval *intervals, unsigned int num_intervals);

#endif
