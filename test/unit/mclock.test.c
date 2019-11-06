/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "unit.h"
#include <stdarg.h>

#include "box/mclock.h"

static void
test_random_stress()
{
	srand(time(NULL));
	plan(1);
	header();
	struct mclock mclock;
	mclock_create(&mclock);
	bool ok = true;
	for (int i = 0; i < 50000; ++i) {
		struct vclock vclock;
		vclock_create(&vclock);
		uint32_t id = rand() % 31 + 1;
		/* Count of non-zero items. */
		int tm = rand() % 31;
		for (int t = 0; t < tm;) {
			uint32_t j = rand() % 31 + 1;
			if (vclock_get(&vclock, j) > 0)
				continue;
			vclock_follow(&vclock, j, rand() + 1);
			++t;
		}
		mclock_update(&mclock, id, &vclock);
		if (!(ok = mclock_check(&mclock)))
			break;
	}
	struct vclock vclock;
	vclock_create(&vclock);
	for (int i = 1; i < 32; ++i)
		mclock_update(&mclock, i, &vclock);
	mclock_destroy(&mclock);
	is(ok, true, "random stress");
	footer();
	check_plan();
}

static void
test_func()
{
	plan(8);
	header();
	struct mclock mclock;
	mclock_create(&mclock);
	struct vclock v1, v2, v3;
	vclock_create(&v1);
	vclock_follow(&v1, 1, 11);
	vclock_follow(&v1, 2, 21);
	vclock_follow(&v1, 3, 31);
	vclock_create(&v2);
	vclock_follow(&v2, 1, 22);
	vclock_follow(&v2, 2, 12);
	vclock_follow(&v2, 3, 30);
	vclock_create(&v3);
	vclock_follow(&v3, 2, 32);
	vclock_follow(&v3, 3, 2);
	vclock_follow(&v3, 4, 5);
	mclock_update(&mclock, 1, &v1);
	mclock_update(&mclock, 2, &v2);
	mclock_update(&mclock, 3, &v3);
	is(mclock_check(&mclock), true, "consistency 1");

	struct vclock v, t;
	vclock_create(&t);
	vclock_follow(&t, 1, 22);
	vclock_follow(&t, 2, 32);
	vclock_follow(&t, 3, 31);
	vclock_follow(&t, 4, 5);

	mclock_get(&mclock, 0, &v);
	is(vclock_compare(&v, &t), 0, "first vclock 1");

	vclock_create(&t);
	vclock_follow(&t, 2, 12);
	vclock_follow(&t, 3, 2);

	mclock_get(&mclock, -1, &v);
	is(vclock_compare(&v, &t), 0, "last vclock 1");

	vclock_create(&t);
	vclock_follow(&t, 1, 11);
	vclock_follow(&t, 2, 21);
	vclock_follow(&t, 3, 30);

	mclock_get(&mclock, 1, &v);
	is(vclock_compare(&v, &t), 0, "second vclock");

	vclock_follow(&v1, 1, 40);
	vclock_follow(&v1, 4, 10);
	mclock_update(&mclock, 1, &v1);
	is(mclock_check(&mclock), true, "consistency 2");
	vclock_follow(&v2, 2, 35);
	vclock_follow(&v2, 4, 3);
	mclock_update(&mclock, 2, &v2);
	is(mclock_check(&mclock), true, "consistency 3");

	vclock_create(&t);
	vclock_follow(&t, 1, 40);
	vclock_follow(&t, 2, 35);
	vclock_follow(&t, 3, 31);
	vclock_follow(&t, 4, 10);

	mclock_get(&mclock, 0, &v);
	is(vclock_compare(&v, &t), 0, "first vclock - 2");

	vclock_create(&t);
	vclock_follow(&t, 2, 21);
	vclock_follow(&t, 3, 2);
	vclock_follow(&t, 4, 3);

	mclock_get(&mclock, -1, &v);
	is(vclock_compare(&v, &t), 0, "last vclock - 2");

	footer();
	check_plan();

}

int main(void)
{
	plan(2);
	test_random_stress();
	test_func();
	check_plan();
}

