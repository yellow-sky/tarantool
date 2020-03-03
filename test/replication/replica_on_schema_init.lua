#!/usr/bin/env tarantool

function trig_local(old, new)
    if new and new[3] == 'test_local' and new[6]['group_id'] ~= 1 then
        return new:update{{'=', 6, {group_id = 1}}}
    end
end

box.ctl.on_schema_init(function()
    box.space._space:before_replace(trig_local)
end)

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
})

require('console').listen(os.getenv('ADMIN'))
