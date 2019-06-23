/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SETTINGS_HPP
#define IROHA_SETTINGS_HPP

#include "ametsuchi/setting_query.hpp"

namespace iroha {

  namespace ametsuchi {

    /**
     * Interface that get all settings from db
     */
    class Settings {
     public:
      virtual ~Settings() = default;

      virtual size_t getMaxDescriptionSize() const = 0;
    };

  } // namespace ametsuchi
} // namespace iroha

#endif  // IROHA_SETTINGS_HPP
