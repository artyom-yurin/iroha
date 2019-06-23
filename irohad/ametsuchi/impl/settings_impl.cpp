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
      setValueFromDB<size_t>(
          setting_query, "MaxDescriptionSize", 64, max_description_size);
    }

    size_t SettingsImpl::getMaxDescriptionSize() const {
      return max_description_size;
    }

    template <typename T>
    void SettingsImpl::setValueFromDB(
        const std::shared_ptr<iroha::ametsuchi::SettingQuery> &setting_query,
        const shared_model::interface::types::SettingKeyType &setting_key,
        T default_value,
        T &field) {
      auto desc_size = setting_query->getSettingValue(setting_key);
      if (desc_size) {
        std::istringstream ss(desc_size.get());
        T num;
        ss >> num;
        if (ss.fail()) {
          field = default_value;
        } else {
          field = num;
        }
      } else {
        field = default_value;
      }
    }

    template <>
    void SettingsImpl::setValueFromDB<shared_model::interface::types::SettingValueType>(
        const std::shared_ptr<iroha::ametsuchi::SettingQuery> &setting_query,
        const shared_model::interface::types::SettingKeyType &setting_key,
        shared_model::interface::types::SettingValueType default_value,
        shared_model::interface::types::SettingValueType &field) {
      auto desc_size = setting_query->getSettingValue(setting_key);
      if (desc_size) {
        field = desc_size.get();
      } else {
        field = default_value;
      }
    }

  } // namespace ametsuchi
} // namespace iroha