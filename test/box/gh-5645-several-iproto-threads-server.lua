#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = 3301,
    memtx_memory = 2 * 1024 * 1024 * 1024,
    iproto_threads = tonumber(arg[1])
})