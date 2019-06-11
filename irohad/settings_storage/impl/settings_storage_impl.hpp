/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SETTINGS_STORAGE_IMPL_HPP
#define IROHA_SETTINGS_STORAGE_IMPL_HPP

#include <shared_mutex>
#include <unordered_map>

#include "settings_storage/settings_storage.hpp"

namespace iroha {

  class MstState;

  class SettingStorageImpl : public SettingStorage {
   public:
    using KeyType = shared_model::interface::types::SettingKeyType;
    using ValueType = shared_model::interface::types::SettingValueType;

    SettingStorageImpl() = default;

    ~SettingStorageImpl() = default;

    boost::optional<ValueType> getSettingValue(
        const KeyType &key) const override;

    void setSettingValue(
        const KeyType &key,
        const ValueType &value) override;

   private:

    /**
     * Storage is map
     */
    std::unordered_map<KeyType, ValueType> storage_;
  };

}  // namespace iroha

#endif  // IROHA_SETTINGS_STORAGE_IMPL_HPP
