/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "backend/protobuf/query_responses/proto_setting_value_response.hpp"

namespace shared_model {
  namespace proto {

    template <typename QueryResponseType>
    SettingValueResponse::SettingValueResponse(QueryResponseType &&queryResponse)
        : CopyableProto(std::forward<QueryResponseType>(queryResponse)),
          setting_value_response_{proto_->setting_value_response()} {}

    template SettingValueResponse::SettingValueResponse(SettingValueResponse::TransportType &);
    template SettingValueResponse::SettingValueResponse(const SettingValueResponse::TransportType &);
    template SettingValueResponse::SettingValueResponse(SettingValueResponse::TransportType &&);

    SettingValueResponse::SettingValueResponse(const SettingValueResponse &o)
        : SettingValueResponse(o.proto_) {}

    SettingValueResponse::SettingValueResponse(SettingValueResponse &&o)
        : SettingValueResponse(std::move(o.proto_)) {}

    const interface::types::SettingValueType &SettingValueResponse::value()
        const {
      return setting_value_response_.value();
    }

  }  // namespace proto
}  // namespace shared_model
