remote = require('net.box')
test_run = require('test_run').new()
fiber = require('fiber')

-- Wrappers to make remote and local execution interface return
-- same result pattern.
--
is_remote = test_run:get_cfg('remote') == 'true'
execute = nil
prepare = nil

test_run:cmd("setopt delimiter ';'")
if is_remote then
    box.schema.user.grant('guest','read, write, execute', 'universe')
    box.schema.user.grant('guest', 'create', 'space')
    cn = remote.connect(box.cfg.listen)
    execute = function(...) return cn:execute(...) end
    prepare = function(...) return cn:prepare(...) end
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
end;

test_run:cmd("setopt delimiter ''");

-- Check default cache statistics.
--
box.info.sql()
box.info:sql()

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
s.sql_str
s.metadata
s.params
s.param_count
execute(s.sql_str, {1, 2})
execute(s.sql_str, {1, 3})

assert(box.info.sql().cache.stmt_count ~= 0)
assert(box.info.sql().cache.size ~= 0)

test_run:cmd("setopt delimiter ';'")
if not is_remote then
    res = s:execute({1, 2})
    assert(res ~= nil)
    res = s:execute({1, 3})
    assert(res ~= nil)
end;
test_run:cmd("setopt delimiter ''");

-- Test preparation of different types of queries.
-- Let's start from DDL. It doesn't make much sense since
-- any prepared DDL statement can be executed once, but
-- anyway make sure that no crashes occur.
--
s = prepare("CREATE INDEX i1 ON test(a)")
execute(s.sql_str)
execute(s.sql_str)

s = prepare("DROP INDEX i1 ON test;")
execute(s.sql_str)
execute(s.sql_str)

s = prepare("CREATE VIEW v AS SELECT * FROM test;")
execute(s.sql_str)
execute(s.sql_str)

s = prepare("DROP VIEW v;")
execute(s.sql_str)
execute(s.sql_str)

s = prepare("ALTER TABLE test RENAME TO test1")
execute(s.sql_str)
execute(s.sql_str)

box.execute("CREATE TABLE test2 (id INT PRIMARY KEY);")
s = prepare("ALTER TABLE test2 ADD CONSTRAINT fk1 FOREIGN KEY (id) REFERENCES test2")
execute(s.sql_str)
execute(s.sql_str)
box.space.TEST2:drop()

s = prepare("CREATE TRIGGER tr1 INSERT ON test1 FOR EACH ROW BEGIN DELETE FROM test1; END;")
execute(s.sql_str)
execute(s.sql_str)

s = prepare("DROP TRIGGER tr1;")
execute(s.sql_str)
execute(s.sql_str)

s = prepare("DROP TABLE test1;")
execute(s.sql_str)
execute(s.sql_str)

-- DQL
--
execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b TEXT)')
space = box.space.TEST
space:replace{1, 2, '3'}
space:replace{4, 5, '6'}
space:replace{7, 8.5, '9'}
_ = prepare("SELECT a FROM test WHERE b = '3';")
s = prepare("SELECT a FROM test WHERE b = '3';")

execute(s.sql_str)
execute(s.sql_str)
test_run:cmd("setopt delimiter ';'")
if not is_remote then
    res = s:execute()
    assert(res ~= nil)
    res = s:execute()
    assert(res ~= nil)
end;
test_run:cmd("setopt delimiter ''");

s = prepare("SELECT count(*), count(a - 3), max(b), abs(id) FROM test WHERE b = '3';")
execute(s.sql_str)
execute(s.sql_str)

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

res = execute(s.sql_str)
res.metadata

-- Workflow with bindings is still the same.
--
s = prepare("SELECT a FROM test WHERE b = ?;")
execute(s.sql_str, {'6'})
execute(s.sql_str, {'9'})

-- DML
s = prepare("INSERT INTO test VALUES (?, ?, ?);")
execute(s.sql_str, {5, 6, '7'})
execute(s.sql_str, {6, 10, '7'})
execute(s.sql_str, {9, 11, '7'})

-- EXPLAIN and PRAGMA work fine as well.
--
s1 = prepare("EXPLAIN SELECT a FROM test WHERE b = '3';")
res = execute(s1.sql_str)
res.metadata
assert(res.rows ~= nil)

s2 = prepare("EXPLAIN QUERY PLAN SELECT a FROM test WHERE b = '3';")
res = execute(s2.sql_str)
res.metadata
assert(res.rows ~= nil)

s3 = prepare("PRAGMA count_changes;")
execute(s3.sql_str)

-- Setting cache size to 0 erases all content from it.
--
box.cfg{sql_cache_size = 0 }
assert(box.info.sql().cache.stmt_count == 0)
assert(box.info.sql().cache.size == 0)
s = prepare("SELECT a FROM test;")
assert(s ~= nil)

-- Still with small size everything should work.
--
box.cfg{sql_cache_size = 1500}

test_run:cmd("setopt delimiter ';'");
for i = 1, 5 do
    pcall(prepare, string.format("SELECT * FROM test WHERE id = %d;", i))
end;
s = prepare("SELECT a FROM test");
assert(s ~= nil);

-- Make sure that if prepared statement is busy (is executed
-- right now), prepared statement is not used, i.e. statement
-- is compiled from scratch, executed and finilized.
--
box.schema.func.create('SLEEP', {language = 'Lua',
    body = 'function () fiber.sleep(0.1) return 1 end',
    exports = {'LUA', 'SQL'}});

s = prepare("SELECT id, SLEEP() FROM test");
assert(s ~= nil);

function implicit_yield()
    execute("SELECT id, SLEEP() FROM test")
end;

f1 = fiber.new(implicit_yield)
f2 = fiber.new(implicit_yield)
f1:set_joinable(true)
f2:set_joinable(true)

f1:join();
f2:join();

if is_remote then
    cn:close()
    box.schema.user.revoke('guest', 'read, write, execute', 'universe')
    box.schema.user.revoke('guest', 'create', 'space')
end;
test_run:cmd("setopt delimiter ''");

box.cfg{sql_cache_size = 5 * 1024 * 1024}
box.space.TEST:drop()
box.schema.func.drop('SLEEP')
