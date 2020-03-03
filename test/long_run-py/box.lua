#!/usr/bin/env tarantool

require('suite')

box.cfg {
    listen            = os.getenv("LISTEN"),
    memtx_memory      = 107374182,
    pid_file          = "tarantool.pid",
}

require('console').listen(os.getenv('ADMIN'))
