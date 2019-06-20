/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main/settings.hpp"
#include "boost/optional.hpp"

Settings::Settings(std::shared_ptr<iroha::ametsuchi::SettingQuery> setting_query){
  setMaxDescriptionSizeFromDB(setting_query, "MaxDescriptionSize");
}

size_t Settings::getMaxDescriptionSize() const{
  return max_description_size;
}

void Settings::setMaxDescriptionSizeFromDB(std::shared_ptr<iroha::ametsuchi::SettingQuery> setting_query,
                                          shared_model::interface::types::SettingKeyType setting_key,
                                          size_t default_value /*= 64*/){
  auto desc_size = setting_query->getSettingValue(setting_key);
  if (desc_size)
  {
    try
    {
      max_description_size = std::stoi(desc_size.get());
    }
    catch(...)
    {
      //If set incorrect value, then the default value
      max_description_size = default_value;
    }
  }
  else
  {
    //If there is no such setting, then the default value
    max_description_size = default_value;
  }
}