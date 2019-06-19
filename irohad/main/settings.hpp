/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SHARED_MODEL_SETTINGS_HPP
#define IROHA_SHARED_MODEL_SETTINGS_HPP

#include "ametsuchi/setting_query.hpp"

/**
 * Class that get all settings from db
 */
class Settings {
 public:
  Settings(std::shared_ptr<iroha::ametsuchi::SettingQuery> setting_query);
  size_t getMaxDescriptionSize() const;
 private:
  void setMaxDescriptionSizeFromDB(std::shared_ptr<iroha::ametsuchi::SettingQuery> setting_query,
                                              shared_model::interface::types::SettingKeyType setting_key,
                                              size_t default_value = 64);
  size_t max_description_size;
};

#endif  // IROHA_SHARED_MODEL_FIELD_VALIDATOR_HPP
