/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SHARED_MODEL_GET_SETTING_VALUE_HPP
#define IROHA_SHARED_MODEL_GET_SETTING_VALUE_HPP

#include "interfaces/base/model_primitive.hpp"
#include "interfaces/common_objects/types.hpp"

namespace shared_model {
  namespace interface {
    /**
     * Get value of setting by a key
     */
    class GetSettingValue : public ModelPrimitive<GetSettingValue> {
     public:
      /**
       * @return key containing requested value
       */
      virtual const types::SettingKeyType &key() const = 0;

      std::string toString() const override;

      bool operator==(const ModelType &rhs) const override;
    };
  }  // namespace interface
}  // namespace shared_model

#endif  // IROHA_SHARED_MODEL_GET_SETTING_VALUE_HPP
