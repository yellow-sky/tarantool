* **Status**: In progress
* **Start date**: 31-03-2020
* **Authors**: Sergey Ostanevich @sergos \<sergos@tarantool.org\>
* **Issues**: https://github.com/tarantool/tarantool/issues/4842

## Summary

The aim of this RFC is to address the following list of problems
formulated at MRG planning meeting:
  - protocol backward compatibility to enable cluster upgrade w/o
    downtime
  - consistency of data on replica and leader
  - switch from leader to replica without data loss
  - up to date replicas to run read-only requests
  - ability to switch async replicas into sync ones and vice versa
  - guarantee of rollback on leader and sync replicas
  - simplicity of cluster orchestration

What this RFC is not:

  - high availability (HA) solution with automated failover, roles
    assignments an so on
  - master-master configuration support

## Background and motivation

There are number of known implementation of consistent data presence in
a Tarantool cluster. They can be commonly named as "wait for LSN"
technique. The biggest issue with this technique is the absence of
rollback guarantees at replica in case of transaction failure on one
master or some of the replicas in the cluster.

To provide such capabilities a new functionality should be introduced in
Tarantool core, with requirements mentioned before - backward
compatibility and ease of cluster orchestration.

The cluster operation is expected to be in a full-mesh topology, although
the process of automated topology support is beyond this RFC.

## Detailed design

### Quorum commit

The main idea behind the proposal is to reuse existent machinery as much
as possible. It will ensure the well-tested and proven functionality
across many instances in MRG and beyond is used. The transaction rollback
mechanism is in place and works for WAL write failure. If we substitute
the WAL success with a new situation which is named 'quorum' later in
this document then no changes to the machinery is needed. The same is
true for snapshot machinery that allows to create a copy of the database
in memory for the whole period of snapshot file write. Adding quorum here
also minimizes changes.

Currently replication represented by the following scheme:
```
Customer        Leader          WAL(L)        Replica        WAL(R)
   |------TXN----->|              |             |              |
   |               |              |             |              |
   |         [TXN undo log        |             |              |
   |            created]          |             |              |
   |               |              |             |              |
   |               |-----TXN----->|             |              |
   |               |              |             |              |
   |               |<---WAL Ok----|             |              |
   |               |              |             |              |
   |         [TXN undo log        |             |              |
   |           destroyed]         |             |              |
   |               |              |             |              |
   |<----TXN Ok----|              |             |              |
   |               |-------Replicate TXN------->|              |
   |               |              |             |              |
   |               |              |       [TXN undo log        |
   |               |              |          created]          |
   |               |              |             |              |
   |               |              |             |-----TXN----->|
   |               |              |             |              |
   |               |              |             |<---WAL Ok----|
   |               |              |             |              |
   |               |              |       [TXN undo log        |
   |               |              |         destroyed]         |
   |               |              |             |              |
```

To introduce the 'quorum' we have to receive confirmation from replicas
to make a decision on whether the quorum is actually present. Leader
collects necessary amount of replicas confirmation plus its own WAL
success. This state is named 'quorum' and gives leader the right to
complete the customers' request. So the picture will change to:
```
Customer        Leader          WAL(L)        Replica        WAL(R)
   |------TXN----->|              |             |              |
   |               |              |             |              |
   |         [TXN undo log        |             |              |
   |            created]          |             |              |
   |               |              |             |              |
   |               |-----TXN----->|             |              |
   |               |              |             |              |
   |               |-------Replicate TXN------->|              |
   |               |              |             |              |
   |               |              |       [TXN undo log        |
   |               |<---WAL Ok----|          created]          |
   |               |              |             |              |
   |           [Waiting           |             |-----TXN----->|
   |         of a quorum]         |             |              |
   |               |              |             |<---WAL Ok----|
   |               |              |             |              |
   |               |<------Replication Ok-------|              |
   |               |              |             |              |
   |            [Quorum           |             |              |
   |           achieved]          |             |              |
   |               |              |             |              |
   |               |---Confirm--->|             |              |
   |               |              |             |              |
   |               |----------Confirm---------->|              |
   |               |              |             |              |
   |<---TXN Ok-----|              |             |---Confirm--->|
   |               |              |             |              |
   |         [TXN undo log        |       [TXN undo log        |
   |           destroyed]         |         destroyed]         |
   |               |              |             |              |
```

The quorum should be collected as a table for a list of transactions
waiting for quorum. The latest transaction that collects the quorum is
considered as complete, as well as all transactions prior to it, since
all transactions should be applied in order. Leader writes a 'confirm'
message to the WAL that refers to the transaction's [LEADER_ID, LSN] and
the confirm has its own LSN. This confirm message is delivered to all
replicas through the existing replication mechanism.

Replica should report a TXN application success to the leader via the
IPROTO explicitly to allow leader to collect the quorum for the TXN.
In case of application failure the replica has to disconnect from the
replication the same way as it is done now. The replica also has to
report its disconnection to the orchestrator. Further actions require
human intervention, since failure means either technical problem (such
as not enough space for WAL) that has to be resolved or an inconsistent
state that requires rejoin.

As soon as leader appears in a situation it has not enough replicas
to achieve quorum, it should stop accepting write requests. There's an
option for leader to rollback to the latest transaction that has quorum:
leader issues a 'rollback' message referring to the [LEADER_ID, LSN]
where LSN is of the first transaction in the leader's undo log. The
rollback message replicated to the available cluster will put it in a
consistent state. After that configuration of the cluster can be
updated to a new available quorum and leader can be switched back to
write mode.

### Leader role assignment.

Be it a user-initiated assignment or an algorithmic one, it should use
a common interface to assign the leader role. By now we implement a
simplified machinery, still it should be feasible in the future to fit
the algorithms, such as RAFT or proposed before box.ctl.promote.

A system space \_voting can be used to replicate the voting among the
cluster, this space should be writable even for a read-only instance.
This space should contain a CURRENT_LEADER_ID at any time - means the
current leader, can be a zero value at the start. This is needed to
compare the  appropriate vclock component below.

All replicas should be subscribed to changes in the space and react as
described below.

 promote(ID) - should be called from a replica with it's own ID.
   Writes an entry in the voting space about this ID is waiting for
   votes from cluster. The entry should also contain the current
   vclock[CURRENT_LEADER_ID] of the nominee.

Upon changes in the space each replica should compare its appropriate
vclock component with submitted one and append its vote to the space:
AYE in case nominee's vclock is bigger or equal to the replica's one,
NAY otherwise.

As soon as nominee collects the quorum for being elected, it claims
himself a Leader by switching in rw mode, writes CURRENT_LEADER_ID as
a FORMER_LEADER_ID in the \_voting space and put its ID as a
CURRENT_LEADER_ID. In case a NAY is appeared in the \_voting or a
timeout predefined in box.cfg is reached, the nominee should remove
it's entry from the space.

The leader should assure that number of available instances in the
cluster is enough to achieve the quorum and proceed to step 3, otherwise
the leader should report the situation of incomplete quorum, as
described in the last paragraph of previous section.

The new Leader has to take the responsibility to replicate former Leader's
entries from its WAL, obtain quorum and commit confirm messages referring
to [FORMER_LEADER_ID, LSN] in its WAL, replicating to the cluster, after
that it can start adding its own entries into the WAL.

 demote(ID) - should be called from the Leader instance.
   The Leader has to switch in ro mode and wait for its' undo log is
   empty. This effectively means all transactions are committed in the
   cluster and it is safe pass the leadership. Then it should write
   CURRENT_LEADER_ID as a FORMER_LEADER_ID and put CURRENT_LEADER_ID
   into 0.

### Recovery and failover.

Tarantool instance during reading WAL should postpone the undo log
deletion until the 'confirm' is read. In case the WAL eof is achieved,
the instance should keep undo log for all transactions that are waiting
for a confirm entry until the role of the instance is set.

If this instance will be assigned a leader role then all transactions
that have no corresponding confirm message should be confirmed (see the
leader role assignment).

In case there's not enough replicas to set up a quorum the cluster can
be switched into a read-only mode. Note, this can't be done by default
since some of transactions can have confirmed state. It is up to human
intervention to force rollback of all transactions that have no confirm
and to put the cluster into a consistent state.

In case the instance will be assigned a replica role, it may appear in
a state that it has conflicting WAL entries, in case it recovered from a
leader role and some of transactions didn't replicated to the current
leader. This situation should be resolved through rejoin of the instance.

Consider an example below. Originally instance with ID1 was assigned a
Leader role and the cluster had 2 replicas with quorum set to 2.

```
+---------------------+---------------------+---------------------+
| ID1                 | ID2                 | ID3                 |
| Leader              | Replica 1           | Replica 2           |
+---------------------+---------------------+---------------------+
| ID1 Tx1             | ID1 Tx1             | ID1 Tx1             |
+---------------------+---------------------+---------------------+
| ID1 Tx2             | ID1 Tx2             | ID1 Tx2             |
+---------------------+---------------------+---------------------+
| ID1 Tx3             | ID1 Tx3             | ID1 Tx3             |
+---------------------+---------------------+---------------------+
| ID1 Conf [ID1, Tx1] | ID1 Conf [ID1, Tx1] |                     |
+---------------------+---------------------+---------------------+
| ID1 Tx4             | ID1 Tx4             |                     |
+---------------------+---------------------+---------------------+
| ID1 Tx5             | ID1 Tx5             |                     |
+---------------------+---------------------+---------------------+
| ID1 Conf [ID1, Tx2] |                     |                     |
+---------------------+---------------------+---------------------+
| Tx6                 |                     |                     |
+---------------------+---------------------+---------------------+
| Tx7                 |                     |                     |
+---------------------+---------------------+---------------------+
```
Suppose at this moment the ID1 instance crashes. Then the ID2 instance
should be assigned a leader role since its ID1 LSN is the biggest.
Then this new leader will deliver its WAL to all replicas.

As soon as quorum for Tx4 and Tx5 will be obtained, it should write the
corresponding Confirms to its WAL. Note that Tx are still uses ID1.
```
+---------------------+---------------------+---------------------+
| ID1                 | ID2                 | ID3                 |
| (dead)              | Leader              | Replica 2           |
+---------------------+---------------------+---------------------+
| ID1 Tx1             | ID1 Tx1             | ID1 Tx1             |
+---------------------+---------------------+---------------------+
| ID1 Tx2             | ID1 Tx2             | ID1 Tx2             |
+---------------------+---------------------+---------------------+
| ID1 Tx3             | ID1 Tx3             | ID1 Tx3             |
+---------------------+---------------------+---------------------+
| ID1 Conf [ID1, Tx1] | ID1 Conf [ID1, Tx1] | ID1 Conf [ID1, Tx1] |
+---------------------+---------------------+---------------------+
| ID1 Tx4             | ID1 Tx4             | ID1 Tx4             |
+---------------------+---------------------+---------------------+
| ID1 Tx5             | ID1 Tx5             | ID1 Tx5             |
+---------------------+---------------------+---------------------+
| ID1 Conf [ID1, Tx2] | ID2 Conf [ID1, Tx5] | ID2 Conf [ID1, Tx5] |
+---------------------+---------------------+---------------------+
| ID1 Tx6             |                     |                     |
+---------------------+---------------------+---------------------+
| ID1 Tx7             |                     |                     |
+---------------------+---------------------+---------------------+
```
After rejoining ID1 will figure out the inconsistency of its WAL: the
last WAL entry it has is corresponding to Tx7, while in Leader's log the
last entry with ID1 is Tx5. Confirm for a Tx can only be issued after
appearance of the Tx on the majoirty of replicas, hence there's a good
chances that ID1 will have inconsistency in its WAL covered with undo
log. So, by rolling back all excessive Txs (in the example they are Tx6
and Tx7) the ID1 can put its memtx and vynil in consistent state.

At this point a snapshot can be created at ID1 with appropriate WAL
rotation. The old WAL should be renamed so it will not be reused in the
future and can be kept for postmortem.
```
+---------------------+---------------------+---------------------+
| ID1                 | ID2                 | ID3                 |
| Replica 1           | Leader              | Replica 2           |
+---------------------+---------------------+---------------------+
| ID1 Tx1             | ID1 Tx1             | ID1 Tx1             |
+---------------------+---------------------+---------------------+
| ID1 Tx2             | ID1 Tx2             | ID1 Tx2             |
+---------------------+---------------------+---------------------+
| ID1 Tx3             | ID1 Tx3             | ID1 Tx3             |
+---------------------+---------------------+---------------------+
| ID1 Conf [ID1, Tx1] | ID1 Conf [ID1, Tx1] | ID1 Conf [ID1, Tx1] |
+---------------------+---------------------+---------------------+
| ID1 Tx4             | ID1 Tx4             | ID1 Tx4             |
+---------------------+---------------------+---------------------+
| ID1 Tx5             | ID1 Tx5             | ID1 Tx5             |
+---------------------+---------------------+---------------------+
|                     | ID2 Conf [Id1, Tx5] | ID2 Conf [Id1, Tx5] |
+---------------------+---------------------+---------------------+
|                     | ID2 Tx1             | ID2 Tx1             |
+---------------------+---------------------+---------------------+
|                     | ID2 Tx2             | ID2 Tx2             |
+---------------------+---------------------+---------------------+
```
Although, in case undo log is not enough to cover the WAL inconsistence
with the new leader, the ID1 needs a complete rejoin.

### Snapshot generation.

We also can reuse current machinery of snapshot generation. Upon
receiving a request to create a snapshot an instance should request a
readview for the current commit operation. Although start of the
snapshot generation should be postponed until this commit operation
receives its confirmation. In case operation is rolled back, the snapshot
generation should be aborted and restarted using current transaction
after rollback is complete.

After snapshot is created the WAL should start from the first operation
that follows the commit operation snapshot is generated for. That means
WAL will contain 'confirm' messages that refer to transactions that are
not present in the WAL. Apparently, we have to allow this for the case
'confirm' refers to a transaction with LSN less than the first entry in
the WAL.

In case master appears unavailable a replica still have to be able to
create a snapshot. Replica can perform rollback for all transactions that
are not confirmed and claim its LSN as the latest confirmed txn. Then it
can create a snapshot in a regular way and start with blank xlog file.
All rolled back transactions will appear through the regular replication
in case master reappears later on.

### Asynchronous replication.

Along with synchronous replicas the cluster can contain asynchronous
replicas. That means async replica doesn't reply to the leader with
errors since they're not contributing into quorum. Still, async
replicas have to follow the new WAL operation, such as keep rollback
info until 'quorum' message is received. This is essential for the case
of 'rollback' message appearance in the WAL. This message assumes
replica is able to perform all necessary rollback by itself. Cluster
information should contain explicit notification of each replica
operation mode.

### Synchronous replication enabling.

Synchronous operation can be required for a set of spaces in the data
scheme. That means only transactions that contain data modification for
these spaces should require quorum. Such transactions named synchronous.
As soon as last operation of synchronous transaction appeared in leader's
WAL, it will cause all following transactions - no matter if they are
synchronous or not - wait for the quorum. In case quorum is not achieved
the 'rollback' operation will cause rollback of all transactions after
the synchronous one. It will ensure the consistent state of the data both
on leader and replicas. In case user doesn't require synchronous operation
for any space then no changes to the WAL generation and replication will
appear.

Cluster description should contain explicit attribute for each replica
to denote it participates in synchronous activities. Also the description
should contain criterion on how many replicas responses are needed to
achieve the quorum.

## Rationale and alternatives

There is an implementation of synchronous replication as part of gh-980
activities, still it is not in a state to get into the product. More
than that it intentionally breaks backward compatibility which is a
prerequisite for this proposal.


