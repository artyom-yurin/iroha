/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SHARED_MODEL_PROTO_SETTING_VALUE_RESPONSE_HPP
#define IROHA_SHARED_MODEL_PROTO_SETTING_VALUE_RESPONSE_HPP

#include "backend/protobuf/common_objects/trivial_proto.hpp"
#include "interfaces/query_responses/setting_value_response.hpp"
#include "qry_responses.pb.h"

namespace shared_model {
  namespace proto {
    class SettingValueResponse final
        : public CopyableProto<interface::SettingValueResponse,
                               iroha::protocol::QueryResponse,
                               SettingValueResponse> {
     public:
      template <typename QueryResponseType>
      explicit SettingValueResponse(QueryResponseType &&queryResponse);

      SettingValueResponse(const SettingValueResponse &o);

      SettingValueResponse(SettingValueResponse &&o);

      const interface::types::SettingValueType &value() const override;

     private:
      const iroha::protocol::SettingValueResponse &setting_value_response_;
    };
  }  // namespace proto
}  // namespace shared_model

#endif  // IROHA_SHARED_MODEL_PROTO_SETTING_VALUE_RESPONSE_HPP
