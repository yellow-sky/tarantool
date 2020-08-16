test_run = require('test_run').new()
SERVERS = {'master_quorum1', 'master_quorum2'}

-- Deploy a cluster.
test_run:create_cluster(SERVERS)
test_run:wait_fullmesh(SERVERS)

test_run:cmd("switch master_quorum1")
repl = box.cfg.replication
for i = 1, 1000 do              \
    box.cfg{replication = ""}   \
    box.cfg{replication = repl} \
end
test_run:cmd("switch default")

-- Cleanup.
test_run:cmd('stop server master_quorum1 with signal=SIGKILL')
test_run:cmd('cleanup server master_quorum1')
test_run:cmd('delete server master_quorum1')
test_run:cmd('stop server master_quorum2 with signal=SIGKILL')
test_run:cmd('cleanup server master_quorum2')
test_run:cmd('delete server master_quorum2')
