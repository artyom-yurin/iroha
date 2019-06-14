/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "backend/protobuf/commands/proto_set_setting_value.hpp"

namespace shared_model {
  namespace proto {

    template <typename CommandType>
    SetSettingValue::SetSettingValue(CommandType &&command)
        : CopyableProto(std::forward<CommandType>(command)),
          set_setting_value_{proto_->set_setting_value()} {}

    template SetSettingValue::SetSettingValue(
        SetSettingValue::TransportType &);
    template SetSettingValue::SetSettingValue(
        const SetSettingValue::TransportType &);
    template SetSettingValue::SetSettingValue(
        SetSettingValue::TransportType &&);

    SetSettingValue::SetSettingValue(const SetSettingValue &o)
        : SetSettingValue(o.proto_) {}

    SetSettingValue::SetSettingValue(SetSettingValue &&o) noexcept
        : SetSettingValue(std::move(o.proto_)) {}

    const interface::types::SettingKeyType &SetSettingValue::key()
        const {
      return set_setting_value_.key();
    }

    const interface::types::SettingValueType &SetSettingValue::value()
        const {
      return set_setting_value_.value();
    }

  }  // namespace proto
}  // namespace shared_model