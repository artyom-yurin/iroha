/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "settings_storage/impl/settings_storage_impl.hpp"

namespace iroha {

  boost::optional<SettingStorageImpl::ValueType>
  SettingStorageImpl::getSettingValue(
      const KeyType &key) const {
    std::unordered_map<SettingStorageImpl::KeyType,SettingStorageImpl::ValueType>::const_iterator
      iter = storage_.find(key);
    if (iter == storage_.end())
    {
      return {};
    }
    else
    {
      return iter->second;
    }
  }

  void SettingStorageImpl::setSettingValue(
    const SettingStorageImpl::KeyType &key,
    const SettingStorageImpl::ValueType &value) {
    storage_[key] = value;
  }

}  // namespace iroha
