/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SETTINGS_IMPL_HPP
#define IROHA_SETTINGS_IMPL_HPP

#include "ametsuchi/settings.hpp"
#include "ametsuchi/setting_query.hpp"

namespace iroha {

  namespace ametsuchi {

    /**
     * Class that get all settings from db
     */
    class SettingsImpl : public Settings {
     public:
      SettingsImpl(
          const std::shared_ptr<iroha::ametsuchi::SettingQuery> &setting_query);

      size_t getMaxDescriptionSize() const override;

     private:
      template <typename T>
      void setValueFromDB(
          const std::shared_ptr<iroha::ametsuchi::SettingQuery> &setting_query,
          const shared_model::interface::types::SettingKeyType &setting_key,
          T default_value,
          T &field);

      size_t max_description_size;
    };
  } // namespace ametsuchi
} // namespace iroha

#endif  // IROHA_SHARED_MODEL_FIELD_VALIDATOR_HPP
