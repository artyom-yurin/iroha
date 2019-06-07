/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "interfaces/query_responses/setting_value_response.hpp"
#include "utils/string_builder.hpp"

namespace shared_model {
  namespace interface {

    std::string SettingValueResponse::toString() const {
      return detail::PrettyStringBuilder()
          .init("SettingValueResponse")
          .append(value())
          .finalize();
    }

    bool SettingValueResponse::operator==(const ModelType &rhs) const {
      return value() == rhs.value();
    }

  }  // namespace interface
}  // namespace shared_model
