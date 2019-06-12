/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SETTINGS_STORAGE_MOCK_HPP
#define IROHA_SETTINGS_STORAGE_MOCK_HPP

#include <gmock/gmock.h>
#include "settings_storage/settings_storage.hpp"

namespace iroha {

  class MockSettingStorage : public SettingStorage {
   public:
    MOCK_CONST_METHOD1(getSettingValue,
                 boost::optional<shared_model::interface::types::SettingValueType>(
                     const shared_model::interface::types::SettingKeyType &key));

    MOCK_METHOD2(setSettingValue,
                 void(const shared_model::interface::types::SettingKeyType & key,
                 const shared_model::interface::types::SettingValueType & value));
  };

}  // namespace iroha

#endif  // IROHA_SETTINGS_STORAGE_MOCK_HPP
