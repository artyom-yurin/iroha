#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#

add_library(settings_storage_test settings_storage_mock.hpp)

target_link_libraries( settings_storage_test
    settings_storage
    shared_model_cryptography
    shared_model_stateless_validation
    shared_model_proto_backend
    shared_model_interfaces_factories
    test_logger
    )