#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(1)

-- This file implements regression tests for sql library.  The
-- focus of this script is testing automatic index creation logic,
-- and specifically creation of automatic partial indexes.

test:do_execsql_test(
    "autoindex1-1.0",
    [[
        CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a INTEGER,b INTEGER);
        INSERT INTO t1 (a, b) VALUES (1, 11);
        INSERT INTO t1 (a, b) VALUES (2, 22);
        INSERT INTO t1 (a, b) SELECT a+2, b+22 FROM t1;
        INSERT INTO t1 (a, b) SELECT a+4, b+44 FROM t1;
        CREATE TABLE t2(id INTEGER PRIMARY KEY AUTOINCREMENT, c INTEGER, d INTEGER);
        INSERT INTO t2 (c, d) SELECT a, 900+b FROM t1;
        SELECT b, d FROM t1 JOIN t2 ON a=c ORDER BY b;
    ]],
    {11, 911, 22, 922, 33, 933, 44, 944, 55, 955, 66, 966, 77, 977, 88, 988})

test:finish_test()
