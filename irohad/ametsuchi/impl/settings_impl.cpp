#include <utility>

#include <utility>

/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "settings_impl.hpp"
#include <sstream>
#include "boost/optional.hpp"

namespace iroha {

  namespace ametsuchi {

    SettingsImpl::SettingsImpl(
        const std::shared_ptr<iroha::ametsuchi::SettingQuery> &setting_query) {
      max_description_size = setValueFromDB<size_t>(setting_query, "MaxDescriptionSize", 64);
    }

    size_t SettingsImpl::getMaxDescriptionSize() const {
      return max_description_size;
    }

    template <typename T>
    T SettingsImpl::setValueFromDB(
        const std::shared_ptr<iroha::ametsuchi::SettingQuery> &setting_query,
        const shared_model::interface::types::SettingKeyType &setting_key,
        T default_value) {
      auto value = setting_query->getSettingValue(setting_key);
      if (value) {
        std::istringstream ss(value.get());
        T num;
        ss >> num;
        if (ss.fail() or ss.tellg() != -1) {
          return default_value;
        } else {
          return num;
        }
      } else {
        return default_value;
      }
    }

    template <>
    shared_model::interface::types::SettingValueType
    SettingsImpl::setValueFromDB<shared_model::interface::types::SettingValueType>(
        const std::shared_ptr<iroha::ametsuchi::SettingQuery> &setting_query,
        const shared_model::interface::types::SettingKeyType &setting_key,
        shared_model::interface::types::SettingValueType default_value) {
      auto value = setting_query->getSettingValue(setting_key);
      if (value) {
        return value.get();
      } else {
        return default_value;
      }
    }

  } // namespace ametsuchi
} // namespace iroha