#include <litmus/reservations/table-driven-common.h>

lt_t td_cur_major_cycle_start(struct table_driven_reservation *tdres)
{
	lt_t x, tmp;

	tmp = tdres->res.env->current_time - tdres->res.env->time_zero;
	x = div64_u64(tmp, tdres->major_cycle);
	x *= tdres->major_cycle;
	return x;
}


lt_t td_next_major_cycle_start(struct table_driven_reservation *tdres)
{
	lt_t x, tmp;

	tmp = tdres->res.env->current_time - tdres->res.env->time_zero;
	x = div64_u64(tmp, tdres->major_cycle) + 1;
	x *= tdres->major_cycle;
	return x;
}

lt_t td_time_remaining_until_end(struct table_driven_reservation *tdres)
{
	lt_t now = tdres->res.env->current_time;
	lt_t end = tdres->cur_interval.end;
	TRACE("td_remaining(%u): start=%llu now=%llu end=%llu state=%d\n",
		tdres->res.id,
		tdres->cur_interval.start,
		now, end,
		tdres->res.state);
	if (now >=  end)
		return 0;
	else
		return end - now;
}
