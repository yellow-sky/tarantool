env = require('test_run')
fiber = require('fiber')
net_box = require('net.box')

test_run = env.new()

test_run:cmd('create server test_server with script="box/gh-5645-several-iproto-threads-server.lua"')
test_run:cmd('create server test_client with script="box/gh-5645-several-iproto-threads-client.lua"')
test_run:cmd('start server test_server with args="1"')
count = 0
test_run:cmd("setopt delimiter ';'")
while test_run:grep_log('test_server', 'stopping input on connection') == nil do
    os.execute("./gh-5645-several-iproto-threads-client.lua&")
    count = count + 1
    fiber.sleep(0.1)
end;
test_run:cmd("setopt delimiter ''");
assert(count < 15)
test_run:cmd('stop server test_server')

test_run:cmd('start server test_server with args="8"')
count = 0
test_run:cmd("setopt delimiter ';'")
while test_run:grep_log('test_server', 'stopping input on connection') == nil and count < 40 do
    os.execute("./gh-5645-several-iproto-threads-client.lua&")
    count = count + 1
end;
test_run:cmd("setopt delimiter ''");
assert(count == 40)
test_run:cmd('stop server test_server')
test_run:cmd("cleanup server test_server")
test_run:cmd("delete server test_client")
test_run:cmd("delete server test_server")

os.execute("killall -s 9 tarantool")






