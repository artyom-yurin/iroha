/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SETTING_QUERY_HPP
#define IROHA_SETTING_QUERY_HPP

#include <boost/optional.hpp>
#include "interfaces/common_objects/types.hpp"

namespace iroha {

  namespace ametsuchi {
    /**
     * Public interface for queries on settings
     */
    class SettingQuery {
     public:
      using SettingKeyType =
          shared_model::interface::types::SettingKeyType;
      using SettingValueType =
          shared_model::interface::types::SettingValueType;

      virtual ~SettingQuery() = default;

      /**
       * Get the setting value with the key
       * @param key is name of the setting
       * @return optional value
       */
      virtual boost::optional<SettingValueType>
      getSettingValue(const SettingKeyType &key) = 0;
    };
  }  // namespace ametsuchi
}  // namespace iroha

#endif  // IROHA_SETTING_QUERY_HPP
