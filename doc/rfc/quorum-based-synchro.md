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

There are number of known implemenatation of consistent data presence in
a cluster. They can be commonly named as "wait for LSN" technique. The
biggest issue with this technique is the abscence of rollback guarantees
at replica in case of transaction failure on one master or some of the
replicas in the cluster.

To provide such capabilities a new functionality should be introduced in
Tarantool core, with requirements mentioned before - backward
compatilibity and ease of cluster orchestration.

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
   |         [TXN Rollback        |             |              |
   |            created]          |             |              |
   |               |              |             |              |
   |               |-----TXN----->|             |              |
   |               |              |             |              |
   |               |<---WAL Ok----|             |              |
   |               |              |             |              |
   |         [TXN Rollback        |             |              |
   |           destroyed]         |             |              |
   |               |              |             |              |
   |<----TXN Ok----|              |             |              |
   |               |-------Replicate TXN------->|              |
   |               |              |             |              |
   |               |              |       [TXN Rollback        |
   |               |              |          created]          |
   |               |              |             |              |
   |               |              |             |-----TXN----->|
   |               |              |             |              |
   |               |              |             |<---WAL Ok----|
   |               |              |             |              |
   |               |              |       [TXN Rollback        |
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
   |         [TXN Rollback        |             |              |
   |            created]          |             |              |
   |               |              |             |              |
   |               |-----TXN----->|             |              |
   |               |              |             |              |
   |               |-------Replicate TXN------->|              |
   |               |              |             |              |
   |               |              |       [TXN Rollback        |
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
   |         [TXN Rollback        |             |              |
   |           destroyed]         |             |              |
   |               |              |             |              |
   |               |---Confirm--->|             |              |
   |               |              |             |              |
   |               |----------Confirm---------->|              |
   |               |              |             |              |
   |<---TXN Ok-----|              |       [TXN Rollback        |
   |               |              |         destroyed]         |
   |               |              |             |              |
   |               |              |             |---Confirm--->|
   |               |              |             |              |
```

The quorum should be collected as a table for a list of transactions
waiting for quorum. The latest transaction that collects the quorum is
considered as complete, as well as all transactions prior to it, since
all transactions should be applied in order. Leader writes a 'confirm'
message to the WAL that refers to the transaction's LSN and it has its
own LSN. This confirm message is delivered to all replicas through the
existing replication mechanism.

Replica should report a positive or a negative result of the TXN to the
leader via the IPROTO explicitly to allow leader to collect the quorum
or anti-quorum for the TXN. In case a negative result for the TXN is
received from minor number of replicas, then leader has to send an error
message to the replicas, which in turn have to disconnect from the
replication the same way as it is done now in case of conflict.

In case leader receives enough error messages to do not achieve the
quorum it should write the 'rollback' message in the WAL. After that
leader and replicas will perform the rollback for all TXN that didn't
receive quorum.

### Recovery and failover.

Tarantool instance during reading WAL should postpone the commit until
the 'confirm' is read. In case the WAL eof is achieved, the instance
should keep rollback for all transactions that are waiting for a confirm
entry until the role of the instance is set. In case this instance
become a replica there are no additional actions needed, since all info
about quorum/rollback will arrive via replication. In case this instance
is assigned a leader role, it should write 'rollback' in its WAL and
perform rollback for all transactions waiting for a quorum.

In case of a leader failure a replica with the biggest LSN with former
leader's ID is elected as a new leader. The replica should record
'rollback' in its WAL which effectively means that all transactions
without quorum should be rolled back. This rollback will be delivered to
all replicas and they will perform rollbacks of all transactions waiting
for quorum.

An interface to force apply pending transactions by issuing a confirm
entry for them have to be introduced for manual recovery.

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
WAL, it will cause all following transactions - matter if they are
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


