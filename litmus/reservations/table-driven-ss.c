#include <linux/sched.h>

#include <litmus/jobs.h>
#include <litmus/litmus.h>
#include <litmus/preempt.h>
#include <litmus/sched_plugin.h>
#include <litmus/reservations/core.h>
#include <litmus/reservations/reservation.h>
#include <litmus/reservations/table-driven.h>
#include <litmus/reservations/table-driven-ss.h>
#include <litmus/reservations/table-driven-common.h>

static void update_execution_cost(
	struct table_driven_reservation *tdres) 
{
	struct reservation_client *client, *next;

	/* Sum non-aperiodic exec_costs */
	lt_t exec_cost_total = 0;
	list_for_each_entry_safe(client, next, &tdres->res.clients, list) {
		struct task_client *tc = container_of(client, struct task_client, client);
		lt_t exec_cost = tsk_rt(tc->task)->task_params.exec_cost;
		exec_cost_total += exec_cost;
	}
	
	/* Sanity check: execution cost should not be zero if there are any real-time tasks */
	BUG_ON(exec_cost_total == list_empty(&tdres->res.clients) ? -1 : 0);

	tdres->expected_exec_cost = max(tdres->expected_exec_cost, exec_cost_total);
	TRACE("update_execution_cost: Expected exec cost of %llu\n", tdres->expected_exec_cost);
}

static void td_ss_client_arrives(
	struct reservation* res,
	struct reservation_client *client
)
{
	struct table_driven_reservation *tdres =
		container_of(res, struct table_driven_reservation, res);

	struct task_client *tc = container_of(client, struct task_client, client);
	struct task_struct *t = tc->task;

	task_class_t class = tsk_rt(t)->task_params.cls;
	TRACE_TASK(t, "td_ss_client_arrives: task class is %d\n", class);

	if (class == RT_CLASS_BEST_EFFORT) {
		TRACE_TASK(t, "td_ss_client_arrives: Adding aperiodic client\n");
		list_add_tail(&client->list, tdres->aperiodic_clients);
	} else {
		lt_t exec_cost = tsk_rt(t)->task_params.exec_cost;
		TRACE_TASK(t, "td_ss_client_arrives: %s Adding normal client with exec_cost %llu\n", 
			!exec_cost ? "WARNING" : "", exec_cost);
		list_add_tail(&client->list, &res->clients);

		update_execution_cost(tdres);
	}

	switch (res->state) {
		case RESERVATION_INACTIVE:
			/* Figure out first replenishment time. */
			tdres->major_cycle_start = td_next_major_cycle_start(tdres);
			res->next_replenishment  = tdres->major_cycle_start;
			res->next_replenishment += tdres->intervals[0].start;
			tdres->next_interval = 0;

			res->env->change_state(res->env, res,
				RESERVATION_DEPLETED);
			break;

		case RESERVATION_ACTIVE:
		case RESERVATION_DEPLETED:
			/* do nothing */
			break;

		case RESERVATION_ACTIVE_IDLE:
			res->env->change_state(res->env, res,
				RESERVATION_ACTIVE);
			break;
	}
}

static void td_ss_client_departs(
	struct reservation *res,
	struct reservation_client *client,
	int did_signal_job_completion
)
{
	struct table_driven_reservation *tdres =
		container_of(res, struct table_driven_reservation, res);

	list_del(&client->list);

	switch (res->state) {
		case RESERVATION_INACTIVE:
		case RESERVATION_ACTIVE_IDLE:
			BUG(); /* INACTIVE or IDLE <=> no client */
			break;

		case RESERVATION_ACTIVE:
			if (list_empty(&res->clients) && list_empty(tdres->aperiodic_clients)) {
				res->env->change_state(res->env, res,
						RESERVATION_ACTIVE_IDLE);
			} /* else: nothing to do, more clients ready */
			break;

		case RESERVATION_DEPLETED:
			/* do nothing */
			break;
	}
}

static void td_ss_replenish(
	struct reservation *res)
{
	struct table_driven_reservation *tdres =
		container_of(res, struct table_driven_reservation, res);

	TRACE("td_ss_replenish(%u): expected_replenishment=%llu\n", res->id,
		res->next_replenishment);

	/* figure out current interval */
	tdres->cur_interval.start = tdres->major_cycle_start +
		tdres->intervals[tdres->next_interval].start;
	tdres->cur_interval.end =  tdres->major_cycle_start +
		tdres->intervals[tdres->next_interval].end;
	TRACE("major_cycle_start=%llu => [%llu, %llu]\n",
		tdres->major_cycle_start,
		tdres->cur_interval.start,
		tdres->cur_interval.end);

	/* reset budget */
	res->cur_budget = td_time_remaining_until_end(tdres);
	res->budget_consumed = 0;
	TRACE("td_ss_replenish(%u): %s budget = %llu\n", res->id,
		res->cur_budget ? "" : "WARNING", res->cur_budget);

	/* reset slack */
	tdres->running_slack = 0;
	tdres->slack_consumed = 0;
	if (res->cur_budget < tdres->expected_exec_cost)
		tdres->cur_slack = 0;
	else
		tdres->cur_slack = res->cur_budget - tdres->expected_exec_cost;
	TRACE("td_ss_replenish(%u): %s slack  = %llu\n", res->id,
		tdres->cur_slack ? "" : "WARNING", tdres->cur_slack);

	/* prepare next slot */
	tdres->next_interval = (tdres->next_interval + 1) % tdres->num_intervals;
	if (!tdres->next_interval)
		/* wrap to next major cycle */
		tdres->major_cycle_start += tdres->major_cycle;

	/* determine next time this reservation becomes eligible to execute */
	res->next_replenishment  = tdres->major_cycle_start;
	res->next_replenishment += tdres->intervals[tdres->next_interval].start;
	TRACE("td_ss_replenish(%u): next_replenishment=%llu\n", res->id,
		res->next_replenishment);


	switch (res->state) {
		case RESERVATION_DEPLETED:
		case RESERVATION_ACTIVE:
		case RESERVATION_ACTIVE_IDLE:
			if (list_empty(&res->clients) && list_empty(tdres->aperiodic_clients))
				res->env->change_state(res->env, res,
					RESERVATION_ACTIVE_IDLE);
			else
				/* we have clients & budget => ACTIVE */
				res->env->change_state(res->env, res,
					RESERVATION_ACTIVE);
			break;

		case RESERVATION_INACTIVE:
			BUG();
			break;
	}
}

static void td_ss_drain_budget(
		struct reservation *res,
		lt_t how_much)
{
	struct table_driven_reservation *tdres =
		container_of(res, struct table_driven_reservation, res);

	res->budget_consumed += how_much;
	res->budget_consumed_total += how_much;

	/* Table-driven scheduling: instead of tracking the budget, we compute
	 * how much time is left in this allocation interval. */

	/* sanity check: we should never try to drain from future slots */
	BUG_ON(tdres->cur_interval.start > res->env->current_time);

	switch (res->state) {
		case RESERVATION_DEPLETED:
		case RESERVATION_INACTIVE:
			BUG();
			break;

		case RESERVATION_ACTIVE_IDLE:
		case RESERVATION_ACTIVE:
			res->cur_budget = td_time_remaining_until_end(tdres);

			TRACE("td_ss_drain_budget(%u): drained to budget = %llu\n",
				res->id, res->cur_budget);

			if (!res->cur_budget) {
				res->env->change_state(res->env, res,
					RESERVATION_DEPLETED);
			} else {
				/* sanity check budget calculation */
				BUG_ON(res->env->current_time >= tdres->cur_interval.end);
				BUG_ON(res->env->current_time < tdres->cur_interval.start);

				/* slack checking */
				if (tdres->running_slack) {
					tdres->slack_consumed += how_much;
					if (how_much >= tdres->running_slack) {
						/* running_slack depleted, preempt until end of interval */
						tdres->running_slack = 0;
					} else {
						tdres->running_slack -= how_much;
					}

					TRACE("td_ss_drain_budget(%u): drained to running slack  = %llu\n",
							res->id, tdres->running_slack);

					env_scheduler_update_after(res->env, tdres->running_slack);
				}
			}

			break;
	}
}

struct task_struct* dispatch_aperiodic_client(
	struct table_driven_reservation *tdres,
	lt_t *for_at_most)
{
	struct reservation_client *client, *next;
	struct task_struct* tsk;

	BUG_ON(tdres->res.state != RESERVATION_ACTIVE);

	/* limit based on current slack */
	*for_at_most = tdres->cur_slack;

	list_for_each_entry_safe(client, next, tdres->aperiodic_clients, list) {
		/* switch the reservation the client is running on
		   reservations are disjoint so this is ok */
		client->reservation = &tdres->res;
		tsk = client->dispatch(client);
		if (likely(tsk)) {
			/* Primitive form of round-robin scheduling: same as default_dispatch_client */
			list_del(&client->list);
			/* move to back of list */
			list_add_tail(&client->list, tdres->aperiodic_clients);

			/* Track worst-case usage of slack as consumed */
			tdres->running_slack = tdres->cur_slack;
			TRACE_TASK(tsk, "td_ss_dispatch_client: dispatching aperiodic client with slack %llu on reservation %d at time %llu\n", 
				tdres->running_slack, tdres->res.id, tdres->res.env->current_time);

			return tsk;
		}
	}
	return NULL;
}

static struct task_struct* td_ss_dispatch_client(
	struct reservation *res,
	lt_t *for_at_most)
{	
	lt_t budget, execution;

	struct task_struct *t;
	struct table_driven_reservation *tdres =
		container_of(res, struct table_driven_reservation, res);

	/* check how much budget we have left in this time slot */
	res->cur_budget = td_time_remaining_until_end(tdres);

	/* check how much slack we have left in this time slot, before deciding who to dispatch */
	budget = res->cur_budget + res->budget_consumed;
	execution = tdres->expected_exec_cost + tdres->slack_consumed;
	if (execution > budget) tdres->cur_slack = 0;
	else tdres->cur_slack = budget - execution;
	
	TRACE("td_ss_dispatch_client: updated slack %llu\n", tdres->cur_slack);

	/* if there is slack available, dispatch an aperiodic client */
	if (tdres->cur_slack > 1 && !list_empty(tdres->aperiodic_clients)) {
		t = dispatch_aperiodic_client(tdres, for_at_most);
	} else {
		t = default_dispatch_client(res, for_at_most);
		TRACE_TASK(t, "td_ss_dispatch_client: dispatching normal client\n");
	}

	/*
	TRACE_TASK(t, "td_ss_dispatch_client(%u): selected, budget=%llu slack=%llu\n",
		res->id, res->cur_budget, tdres->cur_slack);
	*/

	TRACE_TASK(t, "td_ss_dispatch_client(%u): updated to budget=%llu next=%d\n",
		res->id, res->cur_budget, tdres->next_interval);

	if (unlikely(!res->cur_budget)) {
		/* Unlikely case: if we ran out of budget, the user configured
		 * a broken scheduling table (overlapping table slots).
		 * Not much we can do about this, but we can't dispatch a job
		 * now without causing overload. So let's register this reservation
		 * as depleted and wait for the next allocation. */
		TRACE("td_ss_dispatch_client(%u): budget unexpectedly depleted "
			"(check scheduling table for unintended overlap)\n",
			res->id);
		res->env->change_state(res->env, res,
			RESERVATION_DEPLETED);
		return NULL;
	} else
		return t;
}

static struct reservation_ops td_ss_ops = {
	.dispatch_client = td_ss_dispatch_client,
	.client_arrives = td_ss_client_arrives,
	.client_departs = td_ss_client_departs,
	.replenish = td_ss_replenish,
	.drain_budget = td_ss_drain_budget,
};

void table_driven_reservation_ss_init(
	struct table_driven_reservation *tdres_ss,
	struct list_head *aperiodic_clients,
	lt_t major_cycle,
	struct lt_interval *intervals,
	unsigned int num_intervals)
{
	table_driven_reservation_init(tdres_ss, major_cycle, intervals, num_intervals);

	tdres_ss->res.kind = TABLE_DRIVEN_SS;
	tdres_ss->res.ops = &td_ss_ops;

	tdres_ss->expected_exec_cost = 0;
	tdres_ss->slack_consumed = 0;
	tdres_ss->running_slack = 0;
	tdres_ss->cur_slack = 0;

	// all reservations on the same core share the same aperiodic queue
	tdres_ss->aperiodic_clients = aperiodic_clients;
}
