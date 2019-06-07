/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "interfaces/queries/get_setting_value.hpp"

namespace shared_model {
  namespace interface {

    std::string GetSettingValue::toString() const {
      return detail::PrettyStringBuilder()
          .init("GetSettingValue")
          .append("key", key())
          .finalize();
    }

    bool GetSettingValue::operator==(const ModelType &rhs) const {
      return true;
    }

  }  // namespace interface
}  // namespace shared_model
