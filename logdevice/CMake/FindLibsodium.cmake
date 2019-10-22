# Copyright (c) 2017-present, Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

find_path(LIBSODIUM_INCLUDE_DIR NAMES sodium.h)
mark_as_advanced(LIBSODIUM_INCLUDE_DIR)

find_library(LIBSODIUM_LIBRARY NAMES sodium)
mark_as_advanced(LIBSODIUM_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  LIBSODIUM
  REQUIRED_VARS LIBSODIUM_LIBRARY LIBSODIUM_INCLUDE_DIR)

if(LIBSODIUM_FOUND)
  set(LIBSODIUM_LIBRARIES ${LIBSODIUM_LIBRARY})
  set(LIBSODIUM_INCLUDE_DIRS ${LIBSODIUM_INCLUDE_DIR})
  message(STATUS "Found Libsodium: ${LIBSODIUM_LIBRARY}")
endif()