#ifndef LITMUS_RESERVATIONS_TABLE_DRIVEN_BASE_H
#define LITMUS_RESERVATIONS_TABLE_DRIVEN_BASE_H

#include <linux/sched.h>

#include <litmus/litmus.h>
#include <litmus/reservations/reservation.h>
#include <litmus/reservations/table-driven.h>

lt_t td_cur_major_cycle_start(struct table_driven_reservation *tdres);

lt_t td_next_major_cycle_start(struct table_driven_reservation *tdres);

lt_t td_time_remaining_until_end(struct table_driven_reservation *tdres);

#endif