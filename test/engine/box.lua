#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    pid_file            = "tarantool.pid",
    wal_max_size        = 2500,
    memtx_max_tuple_size = 1024 * 1024 * 100,
}

require('console').listen(os.getenv('ADMIN'))
