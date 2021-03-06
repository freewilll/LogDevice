/**
 * Copyright (c) 2017-present, Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
/* can be included multiple times */

#ifndef STAT_DEFINE
  #error STAT_DEFINE() macro not defined
  #define STAT_DEFINE(...)
#endif

// Stats published by Admin server.
STAT_DEFINE(admin_server_started, MAX)

// Maintenances
STAT_DEFINE(num_maintenances, MAX)
STAT_DEFINE(maintenance_state_version, MAX)

STAT_DEFINE(maintenance_progress_UNKNOWN, MAX)
STAT_DEFINE(maintenance_progress_BLOCKED_UNTIL_SAFE, MAX)
STAT_DEFINE(maintenance_progress_IN_PROGRESS, MAX)
STAT_DEFINE(maintenance_progress_COMPLETED, MAX)

// Maintenance Manager
STAT_DEFINE(mm_evaluations, SUM)
STAT_DEFINE(mm_metadata_nodeset_selection_failed, SUM)
STAT_DEFINE(mm_ncm_update_errors, SUM)
STAT_DEFINE(mm_expired_maintenances_removed, SUM)

// Safety Checker (via MM)
STAT_DEFINE(mm_safety_checker_runs, SUM)

// Safety Checker (general)
// The total number of refreshes we have done
STAT_DEFINE(safety_checker_metadata_refreshes, SUM)
STAT_DEFINE(num_historical_epochs, MAX)

#undef STAT_DEFINE
