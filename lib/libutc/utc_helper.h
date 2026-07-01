/* ============================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#ifndef UTC_HELPER_H
#define UTC_HELPER_H

typedef void (*utc_update_cb_t)(int sync_status);

/* brief utc helper library init
* return 0 or -1
*/
int utc_helper_init();

/* brief utc helper library deinit
* return NULL
*/
void utc_helper_deinit();

/* brief get UTC sync status
* input NULL
* output UTC sync status
* return 
*/
int utcGetSyncStatus();

/* brief get last sync UTC time
* input NULL
* output last sync UTC time
* return
*/
uint64_t utcGetLastSyncTime();

/* utcGetUtcTime is obsolete */
uint64_t utcGetUtcTime();

/*register update callback*/
uint64_t utcRegisterUpdateCallback(utc_update_cb_t cb);



#endif