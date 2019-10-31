remote = require('net.box')
test_run = require('test_run').new()
fiber = require('fiber')

-- Wrappers to make remote and local execution interface return
-- same result pattern.
--
is_remote = test_run:get_cfg('remote') == 'true'
execute = nil
prepare = nil
unprepare = nil

test_run:cmd("setopt delimiter ';'")
if is_remote then
    box.schema.user.grant('guest','read, write, execute', 'universe')
    box.schema.user.grant('guest', 'create', 'space')
    cn = remote.connect(box.cfg.listen)
    execute = function(...) return cn:execute(...) end
    prepare = function(...) return cn:prepare(...) end
    unprepare = function(...) return cn:unprepare(...) end
else
    execute = function(...)
        local res, err = box.execute(...)
        if err ~= nil then
            error(err)
        end
        return res
    end
    prepare = function(...)
        local res, err = box.prepare(...)
        if err ~= nil then
            error(err)
        end
        return res
    end
    unprepare = function(...)
        local res, err = box.unprepare(...)
        if err ~= nil then
            error(err)
        end
        return res
    end
end;
test_run:cmd("setopt delimiter ''");

-- Test local interface and basic capabilities of prepared statements.
--
execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b TEXT)')
space = box.space.TEST
space:replace{1, 2, '3'}
space:replace{4, 5, '6'}
space:replace{7, 8.5, '9'}
s, e = prepare("SELECT * FROM test WHERE id = ? AND a = ?;")
assert(e == nil)
assert(s ~= nil)
s.query_id
s.metadata
s.params
s.params_count
execute(s.query_id, {1, 2})
execute(s.query_id, {1, 3})

test_run:cmd("setopt delimiter ';'")
if not is_remote then
    res = s:execute({1, 2})
    assert(res ~= nil)
    res = s:execute({1, 3})
    assert(res ~= nil)
    s:unprepare()
else
    unprepare(s.query_id)
end;
test_run:cmd("setopt delimiter ''");

-- Test preparation of different types of queries.
-- Let's start from DDL. It doesn't make much sense since
-- any prepared DDL statement can be executed once, but
-- anyway make sure that no crashes occur.
--
s = prepare("CREATE INDEX i1 ON test(a)")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

s = prepare("DROP INDEX i1 ON test;")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

s = prepare("CREATE VIEW v AS SELECT * FROM test;")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

s = prepare("DROP VIEW v;")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

s = prepare("ALTER TABLE test RENAME TO test1")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

box.execute("CREATE TABLE test2 (id INT PRIMARY KEY);")
s = prepare("ALTER TABLE test2 ADD CONSTRAINT fk1 FOREIGN KEY (id) REFERENCES test2")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)
box.space.TEST2:drop()

s = prepare("CREATE TRIGGER tr1 INSERT ON test1 FOR EACH ROW BEGIN DELETE FROM test1; END;")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

s = prepare("DROP TRIGGER tr1;")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

s = prepare("DROP TABLE test1;")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

-- DQL
--
execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b TEXT)')
space = box.space.TEST
space:replace{1, 2, '3'}
space:replace{4, 5, '6'}
space:replace{7, 8.5, '9'}
s = prepare("SELECT a FROM test WHERE b = '3';")
execute(s.query_id)
execute(s.query_id)
test_run:cmd("setopt delimiter ';'")
if not is_remote then
    res = s:execute()
    assert(res ~= nil)
    res = s:execute()
    assert(res ~= nil)
end;
test_run:cmd("setopt delimiter ''");

unprepare(s.query_id)

s = prepare("SELECT count(*), count(a - 3), max(b), abs(id) FROM test WHERE b = '3';")
execute(s.query_id)
execute(s.query_id)
unprepare(s.query_id)

-- Let's try something a bit more complicated. For instance recursive
-- query displaying Mandelbrot set.
--
s = prepare([[WITH RECURSIVE \
                  xaxis(x) AS (VALUES(-2.0) UNION ALL SELECT x+0.05 FROM xaxis WHERE x<1.2), \
                  yaxis(y) AS (VALUES(-1.0) UNION ALL SELECT y+0.1 FROM yaxis WHERE y<1.0), \
                  m(iter, cx, cy, x, y) AS ( \
                      SELECT 0, x, y, 0.0, 0.0 FROM xaxis, yaxis \
                      UNION ALL \
                      SELECT iter+1, cx, cy, x*x-y*y + cx, 2.0*x*y + cy FROM m \
                          WHERE (x*x + y*y) < 4.0 AND iter<28), \
                      m2(iter, cx, cy) AS ( \
                          SELECT max(iter), cx, cy FROM m GROUP BY cx, cy), \
                      a(t) AS ( \
                          SELECT group_concat( substr(' .+*#', 1+LEAST(iter/7,4), 1), '') \
                              FROM m2 GROUP BY cy) \
                  SELECT group_concat(TRIM(TRAILING FROM t),x'0a') FROM a;]])

res = execute(s.query_id)
res.metadata
unprepare(s.query_id)

-- Workflow with bindings is still the same.
--
s = prepare("SELECT a FROM test WHERE b = ?;")
execute(s.query_id, {'6'})
execute(s.query_id, {'9'})
unprepare(s.query_id)

-- DML
s = prepare("INSERT INTO test VALUES (?, ?, ?);")
execute(s.query_id, {5, 6, '7'})
execute(s.query_id, {6, 10, '7'})
execute(s.query_id, {9, 11, '7'})
unprepare(s.query_id)

-- EXPLAIN and PRAGMA work fine as well.
--
s1 = prepare("EXPLAIN SELECT a FROM test WHERE b = '3';")
res = execute(s1.query_id)
res.metadata
assert(res.rows ~= nil)

s2 = prepare("EXPLAIN QUERY PLAN SELECT a FROM test WHERE b = '3';")
res = execute(s2.query_id)
res.metadata
assert(res.rows ~= nil)

s3 = prepare("PRAGMA count_changes;")
execute(s3.query_id)

unprepare(s3.query_id)
unprepare(s2.query_id)
unprepare(s1.query_id)

-- Make sure cache memory limit can't be exceeed. We have to
-- create separate fiber (in local mode) since cache is local
-- to session. After cache creation its size is fixed and can't
-- be reconfigured. Also test that ids in each session start from 0.
--
test_run:cmd("setopt delimiter ';'")
box.cfg{sql_cache_size = 3000}
if is_remote then
    cn:close()
    cn = remote.connect(box.cfg.listen)
end;
res = nil;
_ = fiber.create(function()
    s = prepare("SELECT * FROM test;")
    res = s.query_id
end);
while res == nil do fiber.sleep(0.00001) end;
assert(res == 0);

ok = nil
res = nil
_ = fiber.create(function()
    for i = 1, 5 do
        pcall(prepare, "SELECT * FROM test;")
    end
    ok, res = pcall(prepare, "SELECT * FROM test;")
end);
while ok == nil do fiber.sleep(0.00001) end;
assert(ok == false);
res;

-- Make sure cache can be purged with box.session.sql_cache_erase()
--
res = nil;
ok = nil;
_ = fiber.create(function()
    if is_remote then
        cn:eval("box.session.sql_cache_erase()")
    else
        for i = 1, 5 do
            pcall(prepare, "SELECT * FROM test;")
        end
        box.session.sql_cache_erase()
    end
    ok, res = pcall(prepare, "SELECT * FROM test;")
end);
while ok == nil do fiber.sleep(0.00001) end;
assert(ok == true);
assert(res ~= nil);

if is_remote then
    cn:close()
    box.schema.user.revoke('guest', 'read, write, execute', 'universe')
    box.schema.user.revoke('guest', 'create', 'space')
end;

test_run:cmd("setopt delimiter ''");

box.space.TEST:drop()
