#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#

add_library(settings_storage
    impl/settings_storage_impl.cpp
    )

target_link_libraries(settings_storage
    shared_model_interfaces
    )
