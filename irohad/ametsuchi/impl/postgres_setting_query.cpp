/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ametsuchi/impl/postgres_setting_query.hpp"

#include "logger/logger.hpp"

namespace iroha {
  namespace ametsuchi {
    PostgresSettingQuery::PostgresSettingQuery(
        soci::session &sql,
        logger::LoggerPtr log)
        : sql_(sql),
          log_(std::move(log)) {}

    PostgresSettingQuery::PostgresSettingQuery(
        std::unique_ptr<soci::session> sql,
        logger::LoggerPtr log)
        : psql_(std::move(sql)),
          sql_(*psql_),
          log_(std::move(log)) {}

    boost::optional<PostgresSettingQuery::SettingValueType>
      PostgresSettingQuery::getSettingValue(
        const PostgresSettingQuery::SettingKeyType &key) {
      PostgresSettingQuery::SettingValueType value = "";

      try {
        sql_ << "SELECT setting_value FROM setting WHERE setting_key = :key",
            soci::into(value), soci::use(key);
      } catch (const std::exception &e) {
        log_->error("Failed to execute query: {}", e.what());
        return boost::none;
      }

      return boost::make_optional<PostgresSettingQuery::SettingValueType>(
        value);
    }
  }  // namespace ametsuchi
}  // namespace iroha
