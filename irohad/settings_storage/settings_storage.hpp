/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SETTING_STORAGE_HPP
#define IROHA_SETTING_STORAGE_HPP

#include <boost/optional.hpp>
#include "interfaces/common_objects/types.hpp"

namespace iroha {

  /**
   * Interface of storage for settings.
   */
  class SettingStorage{
   public:
    /**
     * Get setting value by the key
     * @param key is the name of setting
     * @return value of setting if setting is present
     */
    virtual boost::optional<shared_model::interface::types::SettingValueType> getSettingValue(
        const shared_model::interface::types::SettingKeyType &key) const = 0;

    /**
     * Set the setting value for the key
     * @param key is the name of setting
     * @param value is the new value for setting
     */
    virtual void setSettingValue(
        const shared_model::interface::types::SettingKeyType & key,
        const shared_model::interface::types::SettingValueType & value) = 0;

    virtual ~SettingStorage() = default;
  };

}  // namespace iroha

#endif  // IROHA_SETTING_STORAGE_HPP