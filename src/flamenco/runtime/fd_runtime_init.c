#include "fd_runtime_init.h"
#include "fd_runtime_err.h"
#include <stdio.h>
#include "../types/fd_types.h"
#include "context/fd_exec_epoch_ctx.h"
#include "context/fd_exec_slot_ctx.h"
#include "../../ballet/lthash/fd_lthash.h"
#include "fd_bank_mgr.h"
#include "fd_system_ids.h"

/* This file must not depend on fd_executor.h */

int
fd_runtime_save_epoch_bank( fd_exec_slot_ctx_t * slot_ctx ) {
  fd_epoch_bank_t * epoch_bank = fd_exec_epoch_ctx_epoch_bank( slot_ctx->epoch_ctx );
  ulong sz = sizeof(uint) + fd_epoch_bank_size(epoch_bank);
  fd_funk_rec_key_t id = fd_runtime_epoch_bank_key();
  int opt_err = 0;
  fd_funk_rec_prepare_t prepare[1];
  fd_funk_t * funk   = slot_ctx->funk;
  fd_funk_rec_t *rec = fd_funk_rec_prepare(funk, slot_ctx->funk_txn, &id, prepare, &opt_err);
  if (NULL == rec)
  {
    FD_LOG_WARNING(("fd_runtime_save_banks failed: %s", fd_funk_strerror(opt_err)));
    return opt_err;
  }

  int funk_err = 0;
  uchar * buf = fd_funk_val_truncate(
      rec,
      fd_funk_alloc( funk ),
      fd_funk_wksp( funk ),
      0UL,
      sz,
      &funk_err );
  if( FD_UNLIKELY( !buf ) ) FD_LOG_ERR(( "fd_funk_val_truncate() failed (%i-%s)", funk_err, fd_funk_strerror( funk_err ) ));
  FD_STORE( uint, buf, FD_RUNTIME_ENC_BINCODE );
  fd_bincode_encode_ctx_t ctx = {
    .data = buf + sizeof(uint),
    .dataend = buf + sz,
  };

  if (FD_UNLIKELY(fd_epoch_bank_encode(epoch_bank, &ctx) != FD_BINCODE_SUCCESS))
  {
    FD_LOG_WARNING(("fd_runtime_save_banks: fd_firedancer_banks_encode failed"));
    fd_funk_rec_cancel( funk, prepare );
    return -1;
  }
  FD_TEST(ctx.data == ctx.dataend);

  fd_funk_rec_publish( funk, prepare );

  FD_LOG_DEBUG(( "epoch frozen, slot=%lu", slot_ctx->slot ));

  return FD_RUNTIME_EXECUTE_SUCCESS;
}

void
fd_runtime_recover_banks( fd_exec_slot_ctx_t * slot_ctx,
                          int                  clear_first,
                          fd_spad_t *          runtime_spad ) {

  fd_funk_t *           funk         = slot_ctx->funk;
  fd_funk_txn_t *       txn          = slot_ctx->funk_txn;
  fd_exec_epoch_ctx_t * epoch_ctx    = slot_ctx->epoch_ctx;
  for(;;) {
    fd_funk_rec_key_t id = fd_runtime_epoch_bank_key();
    fd_funk_rec_query_t query[1];
    fd_funk_rec_t const *rec = fd_funk_rec_query_try_global(funk, txn, &id, NULL, query);
    if (rec == NULL)
      FD_LOG_ERR(("failed to read banks record: missing record"));
    void * val = fd_funk_val( rec, fd_funk_wksp(funk) );

    if( fd_funk_val_sz( rec ) < sizeof(uint) ) {
      FD_LOG_ERR(("failed to read banks record: empty record"));
    }
    uint magic = *(uint*)val;
    if( FD_UNLIKELY( magic!=FD_RUNTIME_ENC_BINCODE ) ) {
      FD_LOG_ERR(( "failed to read banks record: invalid magic number" ));
    }

    if( clear_first ) {
      fd_exec_epoch_ctx_bank_mem_clear( epoch_ctx );
    }

    int err;
    fd_epoch_bank_t * epoch_bank = fd_bincode_decode_spad(
        epoch_bank, runtime_spad,
        (uchar*)val           + sizeof(uint),
        fd_funk_val_sz( rec ) - sizeof(uint),
        &err );
    if( FD_UNLIKELY( err ) ) {
      FD_LOG_WARNING(( "failed to read banks record: invalid decode" ));
      return;
    }

    epoch_ctx->epoch_bank = *epoch_bank;

    FD_LOG_NOTICE(( "recovered epoch_bank" ));

    if( !fd_funk_rec_query_test( query ) ) break;
  }

  ulong * execution_fees = fd_bank_mgr_execution_fees_modify( slot_ctx->bank_mgr );
  *execution_fees = 0;
  fd_bank_mgr_execution_fees_save( slot_ctx->bank_mgr );

  ulong * priority_fees = fd_bank_mgr_priority_fees_modify( slot_ctx->bank_mgr );
  *priority_fees = 0;
  fd_bank_mgr_priority_fees_save( slot_ctx->bank_mgr );

  slot_ctx->txn_count = 0;
  slot_ctx->nonvote_txn_count = 0;
  slot_ctx->failed_txn_count = 0;
  slot_ctx->nonvote_failed_txn_count = 0;
  slot_ctx->total_compute_units_used = 0;
}

void
fd_runtime_delete_banks( fd_exec_slot_ctx_t * slot_ctx ) {
  fd_exec_epoch_ctx_epoch_bank_delete( slot_ctx->epoch_ctx );
}


/* fd_feature_restore loads a feature from the accounts database and
   updates the bank's feature activation state, given a feature account
   address. */

static void
fd_feature_restore( fd_exec_slot_ctx_t *    slot_ctx,
                    fd_feature_id_t const * id,
                    uchar const             acct[ static 32 ],
                    fd_spad_t *             runtime_spad ) {

  /* Skip reverted features */
  if( FD_UNLIKELY( id->reverted ) ) {
    return;
  }

  FD_TXN_ACCOUNT_DECL( acct_rec );
  int err = fd_txn_account_init_from_funk_readonly( acct_rec,
                                                    (fd_pubkey_t *)acct,
                                                    slot_ctx->funk,
                                                    slot_ctx->funk_txn );
  if( FD_UNLIKELY( err!=FD_ACC_MGR_SUCCESS ) ) {
    return;
  }

  /* Skip accounts that are not owned by the feature program
     https://github.com/anza-xyz/solana-sdk/blob/6512aca61167088ce10f2b545c35c9bcb1400e70/feature-gate-interface/src/lib.rs#L42-L44 */
  if( FD_UNLIKELY( memcmp( acct_rec->vt->get_owner( acct_rec ), fd_solana_feature_program_id.key, sizeof(fd_pubkey_t) ) ) ) {
    return;
  }

  /* Account data size must be >= FD_FEATURE_SIZEOF (9 bytes)
     https://github.com/anza-xyz/solana-sdk/blob/6512aca61167088ce10f2b545c35c9bcb1400e70/feature-gate-interface/src/lib.rs#L45-L47 */
  if( FD_UNLIKELY( acct_rec->vt->get_data_len( acct_rec )<FD_FEATURE_SIZEOF ) ) {
    return;
  }

  /* Deserialize the feature account data
     https://github.com/anza-xyz/solana-sdk/blob/6512aca61167088ce10f2b545c35c9bcb1400e70/feature-gate-interface/src/lib.rs#L48-L50 */
  FD_SPAD_FRAME_BEGIN( runtime_spad ) {
    int decode_err;
    fd_feature_t * feature = fd_bincode_decode_spad(
        feature, runtime_spad,
        acct_rec->vt->get_data( acct_rec ),
        acct_rec->vt->get_data_len( acct_rec ),
        &decode_err );
    if( FD_UNLIKELY( decode_err ) ) {
      return;
    }

    if( feature->has_activated_at ) {
      FD_LOG_INFO(( "Feature %s activated at %lu", FD_BASE58_ENC_32_ALLOCA( acct ), feature->activated_at ));
      fd_features_set( &slot_ctx->epoch_ctx->features, id, feature->activated_at );
    } else {
      FD_LOG_DEBUG(( "Feature %s not activated at %lu", FD_BASE58_ENC_32_ALLOCA( acct ), feature->activated_at ));
    }
    /* No need to call destroy, since we are using fd_spad allocator. */
  } FD_SPAD_FRAME_END;
}

void
fd_features_restore( fd_exec_slot_ctx_t * slot_ctx, fd_spad_t * runtime_spad ) {
  for( fd_feature_id_t const * id = fd_feature_iter_init();
                                   !fd_feature_iter_done( id );
                               id = fd_feature_iter_next( id ) ) {
    fd_feature_restore( slot_ctx, id, id->id.key, runtime_spad );
  }
}
