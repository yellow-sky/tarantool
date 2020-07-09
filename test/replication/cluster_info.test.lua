env = require('test_run')
test_run = env.new()

--
-- gh-3363 Track info about replicas
--

test_run:cmd("setopt delimiter ';'")
function compare_cluster_and_cluster_info()
    local FIELD_ID, FIELD_UUID = 1, 2
    local cluster_info = box.space._cluster_info:select()
    local cluster = box.space._cluster:select()
    local diff = {}
    for i, replica in pairs(cluster) do
        local cluster_info_replica = cluster_info[i]
        if replica[FIELD_ID] ~= cluster_info_replica[FIELD_ID] or
            replica[FIELD_UUID] ~= cluster_info_replica[FIELD_UUID] then
            table.insert(diff, {
                _cluster_replica = replica,
                _cluster_info_replica = cluster_info_replica,
            })
        end
    end
    if #diff ~= 0 then
        return false, diff
    end
    return true
end;
test_run:cmd("setopt delimiter ''");

-- Prepare master.
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
box.space.test:insert{1}

-- Check that _cluster_info is similar to _cluster.
compare_cluster_and_cluster_info()

test_run:cmd('create server replica with rpl_master=default, script="replication/replica.lua"')
test_run:cmd("start server replica")

#box.space._cluster_info:select() == 2
compare_cluster_and_cluster_info()

test_run:cmd("stop server replica")
_ = box.space._cluster:delete(2)
compare_cluster_and_cluster_info()

box.schema.user.revoke('guest', 'replication')
test_run:cleanup_cluster()
-- vim: et ts=4 sw=4 sts=4:
