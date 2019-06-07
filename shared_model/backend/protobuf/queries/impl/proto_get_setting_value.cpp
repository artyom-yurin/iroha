/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "backend/protobuf/queries/proto_get_setting_value.hpp"

namespace shared_model {
  namespace proto {

    template <typename QueryType>
    GetSettingValue::GetSettingValue(QueryType &&query)
        : CopyableProto(std::forward<QueryType>(query)),
          setting_value_{proto_->payload().get_setting_value()} {}

    template GetSettingValue::GetSettingValue(GetSettingValue::TransportType &);
    template GetSettingValue::GetSettingValue(const GetSettingValue::TransportType &);
    template GetSettingValue::GetSettingValue(GetSettingValue::TransportType &&);

    GetSettingValue::GetSettingValue(const GetSettingValue &o)
        : GetSettingValue(o.proto_) {}

    GetSettingValue::GetSettingValue(GetSettingValue &&o) noexcept
        : GetSettingValue(std::move(o.proto_)) {}

    const interface::types::SettingKeyType &GetSettingValue::key() const {
      return setting_value_.key();
    }

  }  // namespace proto
}  // namespace shared_model
