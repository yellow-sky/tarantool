* **Status**: In progress
* **Start date**: 27-07-2020
* **Authors**: Sergey Ostanevich @sergos \<sergos@tarantool.org\>, Vladislav Shpilevoy @Gerold103 \<v.shpilevoy@tarantool.org\>, Cyrill Gorcunov @cyrillos \<gorcunov@gmail.com\>
* **Issues**: https://github.com/tarantool/tarantool/issues/5202

## Summary

The #4842 is introduced a quorum based replication (qsync) in Tarantool
environment. To augment the synchronous replication with automated leader
election and failover, we need to make a choice on how to implement one of
algorithms available. Our preference is Raft since it is has good
comprehensibility. I would refer to the https://raft.github.io/raft.pdf further
in the document.

The biggest problem is to link together the master-master nature of log
replication in Tarantool with the strong leader concept in Raft.

## Background and motivation

Customers requested synchronous replication with automated failover as part of
Tarantool development. These features also fits well with Tarantool future we
envision in MRG.

## Detailed design

Qsync implementation is performed in a way that allows users to run
Tarantool-based solution without any changes to their code base. Literally if
you have a database set up, it will continue running after upgrade the same way
as it was prior to 2.5 version. You can start employing the synchronous
replication by introduction of specific spaces in your schema and after that
qsync with come to play. There were no changes to the protocol, so you can mix
2.5 instances with previous in both ways - replicate from new to old either
vice-versa - until you introduce the first synchronous space.

The machinery under the hood oblige all instances to follow a new process of
transaction: if transaction touches a synchronous space then it will require a
special command from the WAL - confirm. Since the obligation is an incremental
requirement we can keep the backward compatibility and in both ways.

We expect to elaborate similar approach to the Raft-based failover machinery.
Which means one can use the qsync replication without the Raft enabled, being
able to elaborate its own failover mechanism. Although, if Raft is enabled then
all instances in the cluster are obliged to follow the rules implied by the
Raft, such as ignore log entries from a leader with stale term number.

### Leader Election

We expect the leader election algorithm can be reused from the Raft
implementation [2] with additional mapping to the Tarantool replication
mechanism.

The replication mechanism of Tarantool with qsync provides the following
features, that required by Raft algorithm:
* The new entry is delivered to the majority of the replicas and only after this
it is committed, the fact of commit is delivered to client
* The log reflects the fact of an entry is committed by a special entry 'Commit'
as part of qsync machinery
* Log consistency comes from the append-only nature of the log and the vclock
check-up during the log append
* Log inconsistencies are handled during replication (see log replication below)

Raft implementation will hold leader's term and it's vote for the current term
in its local structures, while issuing WAL entries to reflect changes in the
status will ensure the persistene of the Raft state. After a fail node will
read the WAL entries and apply them to it's runtime state. These entries should
go into WAL under the ID number 0, so that they will not be propagated.

To propagate the results of election there should be an entry in WAL with
dedicated info: raft_term and leader_id. An elected leader shoud issue such an
entry upon its election as a first entry.


### Log Replication

The qsync RFC explains how we enforce the log replication in a way it is
described in clause 5.3 of the [1]: committed entry always has a commit message
in the xlog. Key difference here is that log entry index comprises of two parts:
the LSN and the served ID. The follower's log consistency will be achieved
during a) leader election, when follower will only fote for a candidate who has
VCLOCK components greater or equal to follower's and b) during the join to a new
leader, when follower will have an option to drop it's waiting queue (named
limbo in qsync implementation), either perform a full rejoin.
The latter is painful, still is the only way to follow the current
representation of xlog that contains no replay info.

There is a requirement in 5.1 of [1]:

> If a server receives a request with a stale term number, it rejects the
> request.

that requires to introduce a machinery that will inspect every DML message from
IPROTO to check if it satisfies the requirement. To introduce this machinery
there should be an additional info put into the replicated DML map upder
IPROTO_RAFT_TERM: a term of the leader taken from the Raft runtime
implementation. This info should be added to the DML - and checked - only in
case the cluster is configured to use the Raft machinery.

As a result an instance that is joining a cluster with the Raft protocol enabled
has to enable the protocol either, otherwise it should be disconnected.

## Rationale and alternatives

C implementation of Raft using binary protocol has an alternative of
implementation using Lua, for example [3]. Although, the performance of the
latter can have a significant impact in part of log replication enforcement.

## References

* [1] https://raft.github.io/raft.pdf
* [2] https://github.com/willemt/raft
* [3] https://github.com/igorcoding/tarantool-raft
