#!/usr/bin/env tarantool

-- get instance name from filename (qsync1.lua => qsync1)
local INSTANCE_ID = string.match(arg[0], "%d")

local SOCKET_DIR = require('fio').cwd()

local TIMEOUT = tonumber(arg[1])

local function instance_uri(instance_id)
    return SOCKET_DIR..'/qsync'..instance_id..'.sock';
end

-- start console first
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = instance_uri(INSTANCE_ID);
    replication_timeout = TIMEOUT;
    replication_sync_lag = 0.01;
    replication_connect_quorum = 3;
    replication = {
        instance_uri(1);
        instance_uri(2);
        instance_uri(3);
        instance_uri(4);
        instance_uri(5);
        instance_uri(6);
        instance_uri(7);
        instance_uri(8);
        instance_uri(9);
        instance_uri(10);
        instance_uri(11);
        instance_uri(12);
        instance_uri(13);
        instance_uri(14);
        instance_uri(15);
        instance_uri(16);
        instance_uri(17);
        instance_uri(18);
        instance_uri(19);
        instance_uri(20);
        instance_uri(21);
        instance_uri(22);
        instance_uri(23);
        instance_uri(24);
        instance_uri(25);
        instance_uri(26);
        instance_uri(27);
        instance_uri(28);
        instance_uri(29);
        instance_uri(30);
        instance_uri(31);
    };
})

box.once("bootstrap", function()
    local test_run = require('test_run').new()
    box.schema.user.grant("guest", 'replication')
    box.schema.space.create('test', {engine = test_run:get_cfg('engine')})
    box.space.test:create_index('primary')
end)
