#ifndef HEADER_fd_src_choreo_policy_fd_policy_h
#define HEADER_fd_src_choreo_policy_fd_policy_h

/* fd_policy implements the policy of the Repair agent.  It determines
   what shreds the validator is expecting but has not yet received and
   needs to request via repair.  It also determines which peer(s) the
   validator should request the shred from.

   The default policy implementation is round-robin DFS: round-robin
   through all the repair peers we know about, and depth-first search
   down the repair forest (see fd_forest.h).

   The policy also dedups identical repair requests that occur within a
   specified amount of time window of each other (configurable).  With
   the DFS strategy, the smaller the tree, the sooner an element will be
   iterated again (when the DFS restarts from the root of the tree). */

#include "fd_forest.h"
#include "fd_repair.h"

/* fd_policy_dedup_ele describes an element in the dedup cache.  The key
   compactly encodes an fd_repair_req_t.

   | kind (4 bits)       | slot (32 bits)  | shred_idx (15 bits) |
   | 0x8 (SHRED)         | slot            | shred_idx           |
   | 0x9 (HIGHEST_SHRED) | slot            | >=shred_idx         |
   | 0xA (ORPHAN)        | orphan slot     | N/A                 |

   Note the common header (sig, from, to, ts, nonce) is not included. */

struct fd_policy_dedup_ele {
  ulong key;      /* compact encoding of fd_repair_req_t detailed above */
  ulong prev;     /* reserved by lru */
  ulong next;     /* reserved by pool and map_chain */
  long  ts;       /* timestamp when the request was sent */
  ulong peer_idx; /* index of the peer to which the request was sent */
};
typedef struct fd_policy_dedup_ele fd_policy_dedup_ele_t;

#define POOL_NAME fd_policy_dedup_pool
#define POOL_T    fd_policy_dedup_ele_t
#include "../../util/tmpl/fd_pool.c"

#define MAP_NAME  fd_policy_dedup_map
#define MAP_ELE_T fd_policy_dedup_ele_t
#include "../../util/tmpl/fd_map_chain.c"

/* fd_policy_dedup implements a dedup cache for already sent Repair
   requests.  It is backed by a map and linked list, in which the least
   recently used (oldest Repair request) in the map is evicted when the
   map is full. */

struct fd_policy_dedup {
  fd_policy_dedup_map_t * map;  /* map of dedup elements */
  fd_policy_dedup_ele_t * pool; /* memory pool of dedup elements */
  fd_policy_dedup_ele_t * lru;  /* singly-linked list of dedup elements by insertion order */
};
typedef struct fd_policy_dedup fd_policy_dedup_t;

/* fd_policy_peers implements the round-robin strategy for selecting
   Repair peers. */

struct fd_policy_peers {
  fd_repair_peer_t * peer_arr; /* array of repair peers */
  fd_repair_peer_t * peer_map; /* map of repair peers (pubkey->peers idx) for O(1) update */
  ulong              peer_cnt; /* count of repair peers */
  ulong              peer_idx; /* round-robin index of the last peer requested from */
};
typedef struct fd_policy_peers fd_policy_peers_t;

struct fd_policy {
  fd_policy_dedup_t * dedup; /* cache (map) of already sent requests */
  fd_policy_peers_t * peers; /* round-robin strategy for selecting repair peers */
  fd_forest_iter_t * 
};
typedef struct fd_policy fd_policy_t;

fd_repair_req_t *
fd_policy_next( fd_policy_t *     policy,
                fd_forest_t *     forest,
                fd_policy_t *     repair,
                fd_repair_req_t * req_out );

#endif /* HEADER_fd_src_choreo_policy_fd_policy_h */
