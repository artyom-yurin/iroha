/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_PROTO_GET_SETTING_VALUE_H
#define IROHA_PROTO_GET_SETTING_VALUE_H

#include "backend/protobuf/common_objects/trivial_proto.hpp"
#include "interfaces/queries/get_setting_value.hpp"
#include "queries.pb.h"

namespace shared_model {
  namespace proto {
    class GetSettingValue final
        : public CopyableProto<interface::GetSettingValue,
                               iroha::protocol::Query,
                               GetSettingValue> {
     public:
      template <typename QueryType>
      explicit GetSettingValue(QueryType &&query);

      GetSettingValue(const GetSettingValue &o);

      GetSettingValue(GetSettingValue &&o) noexcept;

      const interface::types::SettingKeyType &key() const override;

     private:
      // ------------------------------| fields |-------------------------------
      const iroha::protocol::GetSettingValue &setting_value_;
    };

  }  // namespace proto
}  // namespace shared_model

#endif  // IROHA_PROTO_GET_SETTING_VALUE_H
