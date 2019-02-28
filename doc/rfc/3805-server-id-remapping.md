# Server id remapping

* **Status**: In progress
* **Start date**: 15-11-2018
* **Authors**: Serge Petrenko @sergepetrenko \<sergepetrenko@tarantool.org\>,
	       Georgy Kirichenko @GeorgyKirichenkko \<georgy@tarantool.org\>
* **Issues**: \[#3805\](https://github.com/tarantool/tarantool/issues/3805)

## Summary

We allow space \_cluster to be different on different cluster instances.
Different instances will have entries which match by uuid, but have different
ids, for example, one instance may have space \_cluster:
{1, uuid1}, {2, uuid2}, and another one: {1, uuid2}, {2, uuid1}.
Note that we do not make \_cluster a local space. We rely on replication to deliver
\_cluster updates and we introduce before\_replace trigger for space \_cluster to
resolve conflicts. it is covered in more detail below.

Each applier gets an in-memory map of ids. This map is used to translate replica\_id
in rows coming from a remote instance to match \_cluster representation on local
instance prior to applying the rows. Also, for the master to understand replica’s
vclock, applier permutates it using invesre map prior to sending the vclock back.
By default the map is identical and no changes are performed. It becomes
non-trivial only when some conflict resolution happens, as described below.
For example, applier on the first instance in the example above would have a
map {1:2, 2:1}. This would mean that rows signed with id=2 from a remote instance
correspond to an instance with id=1 (both of them refer to an instance with uuid1),
and vice versa.

## Background and motivation

Remapping server ids is needed for conflict resolution, which takes place in
2 cases:
  a) imagine a situation with 2 instances, A and B, configured in master-master
     replication. Then 2 replicas, C and D connect to the masters, C to A and D
     to B, respectively, and do that almost simultaneously, so that one's
     registration in _cluster by its master doesn't arrive to the other master
     yet. This will make the masters, providing that they are in consistent
     state prior to the replicas join, generate conflicting _cluster entries by
     assigning the same id to 2 replicas with differing UUIDs.
  b) the main idea of anonymous replicas is that they do not pollute \_cluster
     table of other non-anonymousc cluster members. But it would be nice for an
     anonymous replica to add itself to \_cluster locally, so that it would have
     an id to sign its local space updates in the WAL. Other instances do not
     know about an id occupied by the anonymous replica locally, so they can
     easily generate a _cluster entry with the same id. Some remapping needs to
     happen on anonymous replica side to escape errors.

## Detailed design

We introduce a before\_replace trigger for space \_cluster, which will serve 2 purposes:
a) Perform conflict resolution:
   say, we have an entry {5, uuid5} in our \_cluster and we get a row {5, uuid6}
   from a remote instance. We will replace the row with {6, uuid6} locally.
   To do so we need to change the primary key for space \_cluster to index by
   uuid instead of indexing by id as it is done now (This is needed because it’s
   impossible to alter primary key fields in before\_replace triggers).
b) Update applier maps:
   When resolving a conflict like the one mentioned in (a), we add {5:6} to the applier’s map.
   To find the corresponding applier, we introduce some global variable to store
   the last applier which issued such an update so that the before\_replace
   trigger knows which applier’s map to modify. Ordering \_cluster updates shouldn’t
   affect performance since \_cluster operations are rather rare.

If we receive a row with a uuid we already have, we do not insert it,
but just update the applier’s map accordingly.

### Updating the applier maps and spreading \_cluster updates
Since we rely on replication to deliver us remote instance's \_cluster changes,
and we need to update every appliers map, we have to process all the rows,
corresponding to the same \_cluster update coming from every instance.

Here's what we'll do:
We introduce a new replication group, say GROUP\_CLUSTER, and every row from
\_cluster belongs to this group. It is needed for early detection of rows
containing \_cluster updates. When applier sees such a row, it issues a NOP to
follow remote instance's vclock and generates its own \_cluster entry, copying
the original one, but containing replica\_id field equal to the local instance's
id. When the row is inserted, the before\_replace trigger will take care of
conflict resolution and of updating current applier's map.

This ensures that every instance delivers its changes in \_cluster to every
subscribed instance. To stop the infinite replication loop, when receiving a row
with a uuid already present in \_cluster, just update the applier's map with a
new id, and don't issue own \_cluster update.

### Restoring applier maps after power-off
Let's suppose a replica has successfully joined and subsribed. Then it is powered
off. When it's turned on back again, its applier maps are empty, and it won't be
able to substitue replica\_id field in incoming rows prior to applying them and
all the updates from different instances will be mixed up.
Let's first see what happens in more detail. Replica and master have the same
\_cluster entries issued before the latest master snapshot, but their entries after
that point in time till replica vclock at subscribe may differ. So, to initialize
the applier map on replica, master has to send all its \_cluster entries which were
updated during the period of time after the latest snapshot till replica vclock
at subscribe. After receiving the rows and making corresponding changes in applier
map (without making any changes to \_cluster, cos it already has these entries, but
with different ids), replica proceeds to normal subscribe.

To create such behaviour we add a new field to space \_cluster: lsn of the last
update of the tuple. On subscribe, before feeding replica a stream of rows, we
feed it all the rows in \_cluster which got changed in a time window since the last
checkpoint to replica vclock at subscribe.
If the entry in \_cluster is deleted, there is no need to mark it in any special
way, it will also already be deleted on the replica.

## Open questions

1. If we try to subscribe after power off, applier map is empty, and to fill it
   we need to follow the steps mentioned above. However, to send subscribe vclock
   we already need to have an applier map.
   We somehow need to permutate vclock at subscribe before sending it to master.
   We still need to figure out how to do that.

## Rationale and alternatives

1. Instead of issuing NOPs and our own \_cluster updates when receiving a remote
   \_cluster update, we can use GROUP\_CLUSTER to skip lsn check and explicitly
   process the same row coming from all the instances.
   Then we have to deal with trying to follow same vclock multiple times.
2. Instead of using GROUP\_CLUSTER to identify rows corresponding to space
   \_cluster, we could introcduce a special flag, or parse every row in applier
   and check space\_id.
