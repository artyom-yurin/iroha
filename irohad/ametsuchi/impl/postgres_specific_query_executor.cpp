/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ametsuchi/impl/postgres_specific_query_executor.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/format.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/range/irange.hpp>
#include "ametsuchi/block_storage.hpp"
#include "ametsuchi/impl/soci_utils.hpp"
#include "backend/plain/account_detail_record_id.hpp"
#include "backend/plain/peer.hpp"
#include "common/bind.hpp"
#include "common/byteutils.hpp"
#include "interfaces/common_objects/amount.hpp"
#include "interfaces/iroha_internal/block.hpp"
#include "interfaces/permission_to_string.hpp"
#include "interfaces/queries/asset_pagination_meta.hpp"
#include "interfaces/queries/get_account.hpp"
#include "interfaces/queries/get_account_asset_transactions.hpp"
#include "interfaces/queries/get_account_assets.hpp"
#include "interfaces/queries/get_account_detail.hpp"
#include "interfaces/queries/get_account_transactions.hpp"
#include "interfaces/queries/get_asset_info.hpp"
#include "interfaces/queries/get_block.hpp"
#include "interfaces/queries/get_peers.hpp"
#include "interfaces/queries/get_pending_transactions.hpp"
#include "interfaces/queries/get_role_permissions.hpp"
#include "interfaces/queries/get_roles.hpp"
#include "interfaces/queries/get_signatories.hpp"
#include "interfaces/queries/get_transactions.hpp"
#include "interfaces/queries/query.hpp"
#include "interfaces/queries/tx_pagination_meta.hpp"
#include "interfaces/transaction.hpp"
#include "logger/logger.hpp"
#include "pending_txs_storage/pending_txs_storage.hpp"

using namespace shared_model::interface::permissions;

namespace {

  using namespace iroha;

  const auto kRootRolePermStr =
      shared_model::interface::RolePermissionSet({Role::kRoot}).toBitstring();

  shared_model::interface::types::DomainIdType getDomainFromName(
      const shared_model::interface::types::AccountIdType &account_id) {
    // TODO 03.10.18 andrei: IR-1728 Move getDomainFromName to shared_model
    std::vector<std::string> res;
    boost::split(res, account_id, boost::is_any_of("@"));
    return res.at(1);
  }

  std::string getAccountRolePermissionCheckSql(
      shared_model::interface::permissions::Role permission,
      const std::string &account_alias = ":role_account_id") {
    const auto perm_str =
        shared_model::interface::RolePermissionSet({permission}).toBitstring();
    const auto bits = shared_model::interface::RolePermissionSet::size();
    // TODO 14.09.18 andrei: IR-1708 Load SQL from separate files
    std::string query = (boost::format(R"(
          SELECT
            (
              COALESCE(bit_or(rp.permission), '0'::bit(%1%))
              & ('%2%'::bit(%1%) | '%3%'::bit(%1%))
            ) != '0'::bit(%1%)
            AS perm
          FROM role_has_permissions AS rp
          JOIN account_has_roles AS ar on ar.role_id = rp.role_id
          WHERE ar.account_id = %4%)")
                         % bits % perm_str % kRootRolePermStr % account_alias)
                            .str();
    return query;
  }

  /**
   * Generate an SQL subquery which checks if creator has corresponding
   * permissions for target account
   * It verifies individual, domain, and global permissions, and returns true if
   * any of listed permissions is present
   */
  auto hasQueryPermission(
      const shared_model::interface::types::AccountIdType &creator,
      const shared_model::interface::types::AccountIdType &target_account,
      Role indiv_permission_id,
      Role all_permission_id,
      Role domain_permission_id) {
    const auto bits = shared_model::interface::RolePermissionSet::size();
    const auto perm_str =
        shared_model::interface::RolePermissionSet({indiv_permission_id})
            .toBitstring();
    const auto all_perm_str =
        shared_model::interface::RolePermissionSet({all_permission_id})
            .toBitstring();
    const auto domain_perm_str =
        shared_model::interface::RolePermissionSet({domain_permission_id})
            .toBitstring();

    const std::string creator_quoted{(boost::format("'%s'") % creator).str()};

    boost::format cmd(R"(
    WITH
        has_root_perm AS (%1%),
        has_indiv_perm AS (
          SELECT (COALESCE(bit_or(rp.permission), '0'::bit(%2%))
          & '%4%') = '%4%' FROM role_has_permissions AS rp
              JOIN account_has_roles AS ar on ar.role_id = rp.role_id
              WHERE ar.account_id = '%3%'
        ),
        has_all_perm AS (
          SELECT (COALESCE(bit_or(rp.permission), '0'::bit(%2%))
          & '%5%') = '%5%' FROM role_has_permissions AS rp
              JOIN account_has_roles AS ar on ar.role_id = rp.role_id
              WHERE ar.account_id = '%3%'
        ),
        has_domain_perm AS (
          SELECT (COALESCE(bit_or(rp.permission), '0'::bit(%2%))
          & '%6%') = '%6%' FROM role_has_permissions AS rp
              JOIN account_has_roles AS ar on ar.role_id = rp.role_id
              WHERE ar.account_id = '%3%'
        )
    SELECT (SELECT * from has_root_perm)
        OR ('%3%' = '%7%' AND (SELECT * FROM has_indiv_perm))
        OR (SELECT * FROM has_all_perm)
        OR ('%8%' = '%9%' AND (SELECT * FROM has_domain_perm)) AS perm
    )");

    return (cmd % getAccountRolePermissionCheckSql(Role::kRoot, creator_quoted)
            % bits % creator % perm_str % all_perm_str % domain_perm_str
            % target_account % getDomainFromName(creator)
            % getDomainFromName(target_account))
        .str();
  }

  /// Query result is a tuple of optionals, since there could be no entry
  template <typename... Value>
  using QueryType = boost::tuple<boost::optional<Value>...>;

  /**
   * Create an error response in case user does not have permissions to perform
   * a query
   * @tparam Roles - type of roles
   * @param roles, which user lacks
   * @return lambda returning the error response itself
   */
  template <typename... Roles>
  auto notEnoughPermissionsResponse(
      std::shared_ptr<shared_model::interface::PermissionToString>
          perm_converter,
      Roles... roles) {
    return [perm_converter, roles...] {
      std::string error = "user must have at least one of the permissions: ";
      for (auto role : {roles...}) {
        error += perm_converter->toString(role) + ", ";
      }
      return error;
    };
  }

  static const std::string kEmptyDetailsResponse{"{}"};

  template <typename T>
  auto resultWithoutNulls(T range) {
    return range | boost::adaptors::transformed([](auto &&t) {
             return iroha::ametsuchi::rebind(t);
           })
        | boost::adaptors::filtered(
               [](const auto &t) { return static_cast<bool>(t); })
        | boost::adaptors::transformed([](auto t) { return *t; });
  }

}  // namespace

namespace iroha {
  namespace ametsuchi {

    PostgresSpecificQueryExecutor::PostgresSpecificQueryExecutor(
        soci::session &sql,
        BlockStorage &block_store,
        std::shared_ptr<PendingTransactionStorage> pending_txs_storage,
        std::shared_ptr<shared_model::interface::QueryResponseFactory>
            response_factory,
        std::shared_ptr<shared_model::interface::PermissionToString>
            perm_converter,
        logger::LoggerPtr log)
        : sql_(sql),
          block_store_(block_store),
          pending_txs_storage_(std::move(pending_txs_storage)),
          query_response_factory_{std::move(response_factory)},
          perm_converter_(std::move(perm_converter)),
          log_(std::move(log)) {}

    QueryExecutorResult PostgresSpecificQueryExecutor::execute(
        const shared_model::interface::Query &qry) {
      return boost::apply_visitor(
          [this, &qry](const auto &query) {
            return (*this)(query, qry.creatorAccountId(), qry.hash());
          },
          qry.get());
    }

    template <typename RangeGen, typename Pred>
    std::vector<std::unique_ptr<shared_model::interface::Transaction>>
    PostgresSpecificQueryExecutor::getTransactionsFromBlock(
        uint64_t block_id, RangeGen &&range_gen, Pred &&pred) {
      std::vector<std::unique_ptr<shared_model::interface::Transaction>> result;
      auto block = block_store_.fetch(block_id);
      if (not block) {
        log_->error("Failed to retrieve block with id {}", block_id);
        return result;
      }

      boost::transform(range_gen(boost::size((*block)->transactions()))
                           | boost::adaptors::transformed(
                                 [&block](auto i) -> decltype(auto) {
                                   return (*block)->transactions()[i];
                                 })
                           | boost::adaptors::filtered(pred),
                       std::back_inserter(result),
                       [&](const auto &tx) { return clone(tx); });

      return result;
    }

    template <typename QueryTuple,
              typename PermissionTuple,
              typename QueryExecutor,
              typename ResponseCreator,
              typename PermissionsErrResponse>
    QueryExecutorResult PostgresSpecificQueryExecutor::executeQuery(
        QueryExecutor &&query_executor,
        const shared_model::interface::types::HashType &query_hash,
        ResponseCreator &&response_creator,
        PermissionsErrResponse &&perms_err_response) {
      using T = concat<QueryTuple, PermissionTuple>;
      try {
        soci::rowset<T> st = std::forward<QueryExecutor>(query_executor)();
        auto range = boost::make_iterator_range(st.begin(), st.end());

        return iroha::ametsuchi::apply(
            viewPermissions<PermissionTuple>(range.front()),
            [this, range, &response_creator, &perms_err_response, &query_hash](
                auto... perms) {
              bool temp[] = {not perms...};
              if (std::all_of(std::begin(temp), std::end(temp), [](auto b) {
                    return b;
                  })) {
                // TODO [IR-1816] Akvinikym 03.12.18: replace magic number 2
                // with a named constant
                return this->logAndReturnErrorResponse(
                    QueryErrorType::kStatefulFailed,
                    std::forward<PermissionsErrResponse>(perms_err_response)(),
                    2,
                    query_hash);
              }
              auto query_range =
                  range | boost::adaptors::transformed([](auto &t) {
                    return viewQuery<QueryTuple>(t);
                  });
              return std::forward<ResponseCreator>(response_creator)(
                  query_range, perms...);
            });
      } catch (const std::exception &e) {
        return this->logAndReturnErrorResponse(
            QueryErrorType::kStatefulFailed, e.what(), 1, query_hash);
      }
    }

    bool PostgresSpecificQueryExecutor::hasAccountRolePermission(
        shared_model::interface::permissions::Role permission,
        const std::string &account_id) const {
      using T = boost::tuple<int>;
      boost::format cmd(R"(%s)");
      try {
        soci::rowset<T> st =
            (sql_.prepare
                 << (cmd % getAccountRolePermissionCheckSql(permission)).str(),
             soci::use(account_id, "role_account_id"));
        return st.begin()->get<0>();
      } catch (const std::exception &e) {
        log_->error("Failed to validate query: {}", e.what());
        return false;
      }
    }

    std::unique_ptr<shared_model::interface::QueryResponse>
    PostgresSpecificQueryExecutor::logAndReturnErrorResponse(
        QueryErrorType error_type,
        QueryErrorMessageType error_body,
        QueryErrorCodeType error_code,
        const shared_model::interface::types::HashType &query_hash) const {
      std::string error;
      switch (error_type) {
        case QueryErrorType::kNoAccount:
          error = "could find account with such id: " + error_body;
          break;
        case QueryErrorType::kNoSignatories:
          error = "no signatories found in account with such id: " + error_body;
          break;
        case QueryErrorType::kNoAccountDetail:
          error = "no details in account with such id: " + error_body;
          break;
        case QueryErrorType::kNoRoles:
          error =
              "no role with such name in account with such id: " + error_body;
          break;
        case QueryErrorType::kNoAsset:
          error =
              "no asset with such name in account with such id: " + error_body;
          break;
          // other errors are either handled by generic response or do not
          // appear yet
        default:
          error = "failed to execute query: " + error_body;
          break;
      }

      log_->error("{}", error);
      return query_response_factory_->createErrorQueryResponse(
          error_type, error, error_code, query_hash);
    }

    template <typename Query,
              typename QueryChecker,
              typename QueryApplier,
              typename... Permissions>
    QueryExecutorResult PostgresSpecificQueryExecutor::executeTransactionsQuery(
        const Query &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash,
        QueryChecker &&qry_checker,
        const std::string &related_txs,
        QueryApplier applier,
        Permissions... perms) {
      using QueryTuple = QueryType<shared_model::interface::types::HeightType,
                                   uint64_t,
                                   uint64_t>;
      using PermissionTuple = boost::tuple<int>;
      const auto &pagination_info = q.paginationMeta();
      auto first_hash = pagination_info.firstTxHash();
      // retrieve one extra transaction to populate next_hash
      auto query_size = pagination_info.pageSize() + 1u;

      auto base = boost::format(R"(WITH has_perms AS (%s),
      my_txs AS (%s),
      first_hash AS (%s),
      total_size AS (
        SELECT COUNT(*) FROM my_txs
      ),
      t AS (
        SELECT my_txs.height, my_txs.index
        FROM my_txs JOIN
        first_hash ON my_txs.height > first_hash.height
        OR (my_txs.height = first_hash.height AND
            my_txs.index >= first_hash.index)
        LIMIT :page_size
      )
      SELECT height, index, count, perm FROM t
      RIGHT OUTER JOIN has_perms ON TRUE
      JOIN total_size ON TRUE
      )");

      // select tx with specified hash
      auto first_by_hash = R"(SELECT height, index FROM position_by_hash
      WHERE hash = :hash LIMIT 1)";

      // select first ever tx
      auto first_tx = R"(SELECT height, index FROM position_by_hash
      ORDER BY height, index ASC LIMIT 1)";

      auto cmd = base % hasQueryPermission(creator_id, q.accountId(), perms...)
          % related_txs;
      if (first_hash) {
        cmd = base % first_by_hash;
      } else {
        cmd = base % first_tx;
      }

      auto query = cmd.str();

      return executeQuery<QueryTuple, PermissionTuple>(
          applier(query),
          query_hash,
          [&](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            uint64_t total_size = 0;
            if (not boost::empty(range_without_nulls)) {
              total_size = boost::get<2>(*range_without_nulls.begin());
            }
            std::map<uint64_t, std::vector<uint64_t>> index;
            // unpack results to get map from block height to index of tx in
            // a block
            for (const auto &t : range_without_nulls) {
              iroha::ametsuchi::apply(
                  t, [&index](auto &height, auto &idx, auto &) {
                    index[height].push_back(idx);
                  });
            }

            std::vector<std::unique_ptr<shared_model::interface::Transaction>>
                response_txs;
            // get transactions corresponding to indexes
            for (auto &block : index) {
              auto txs = this->getTransactionsFromBlock(
                  block.first,
                  [&block](auto) { return block.second; },
                  [](auto &) { return true; });
              std::move(
                  txs.begin(), txs.end(), std::back_inserter(response_txs));
            }

            if (response_txs.empty()) {
              if (first_hash) {
                // if 0 transactions are returned, and there is a specified
                // paging hash, we assume it's invalid, since query with valid
                // hash is guaranteed to return at least one transaction
                auto error = (boost::format("invalid pagination hash: %s")
                              % first_hash->hex())
                                 .str();
                return this->logAndReturnErrorResponse(
                    QueryErrorType::kStatefulFailed, error, 4, query_hash);
              }
              // if paging hash is not specified, we should check, why 0
              // transactions are returned - it can be because there are
              // actually no transactions for this query or some of the
              // parameters were wrong
              if (auto query_incorrect =
                      std::forward<QueryChecker>(qry_checker)(q)) {
                return this->logAndReturnErrorResponse(
                    QueryErrorType::kStatefulFailed,
                    query_incorrect.error_message,
                    query_incorrect.error_code,
                    query_hash);
              }
            }

            // if the number of returned transactions is equal to the
            // page size + 1, it means that the last transaction is the
            // first one in the next page and we need to return it as
            // the next hash
            if (response_txs.size() == query_size) {
              auto next_hash = response_txs.back()->hash();
              response_txs.pop_back();
              return query_response_factory_->createTransactionsPageResponse(
                  std::move(response_txs), next_hash, total_size, query_hash);
            }

            return query_response_factory_->createTransactionsPageResponse(
                std::move(response_txs), boost::none, total_size, query_hash);
          },
          notEnoughPermissionsResponse(perm_converter_, perms...));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetAccount &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple =
          QueryType<shared_model::interface::types::AccountIdType,
                    shared_model::interface::types::DomainIdType,
                    shared_model::interface::types::QuorumType,
                    shared_model::interface::types::DetailType,
                    std::string>;
      using PermissionTuple = boost::tuple<int>;

      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      t AS (
          SELECT a.account_id, a.domain_id, a.quorum, a.data, ARRAY_AGG(ar.role_id) AS roles
          FROM account AS a, account_has_roles AS ar
          WHERE a.account_id = :target_account_id
          AND ar.account_id = a.account_id
          GROUP BY a.account_id
      )
      SELECT account_id, domain_id, quorum, data, roles, perm
      FROM t RIGHT OUTER JOIN has_perms AS p ON TRUE
      )")
                  % hasQueryPermission(creator_id,
                                       q.accountId(),
                                       Role::kGetMyAccount,
                                       Role::kGetAllAccounts,
                                       Role::kGetDomainAccounts))
                     .str();

      auto query_apply = [this, &query_hash](auto &account_id,
                                             auto &domain_id,
                                             auto &quorum,
                                             auto &data,
                                             auto &roles_str) {
        std::vector<shared_model::interface::types::RoleIdType> roles;
        auto roles_str_no_brackets = roles_str.substr(1, roles_str.size() - 2);
        boost::split(
            roles, roles_str_no_brackets, [](char c) { return c == ','; });
        return query_response_factory_->createAccountResponse(
            account_id, domain_id, quorum, data, std::move(roles), query_hash);
      };

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd,
                    soci::use(q.accountId(), "target_account_id"));
          },
          query_hash,
          [this, &q, &query_apply, &query_hash](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            if (range_without_nulls.empty()) {
              return this->logAndReturnErrorResponse(
                  QueryErrorType::kNoAccount, q.accountId(), 0, query_hash);
            }

            return iroha::ametsuchi::apply(range_without_nulls.front(),
                                           query_apply);
          },
          notEnoughPermissionsResponse(perm_converter_,
                                       Role::kGetMyAccount,
                                       Role::kGetAllAccounts,
                                       Role::kGetDomainAccounts));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetBlock &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      if (not hasAccountRolePermission(Role::kGetBlocks, creator_id)) {
        // no permission
        return query_response_factory_->createErrorQueryResponse(
            shared_model::interface::QueryResponseFactory::ErrorQueryType::
                kStatefulFailed,
            notEnoughPermissionsResponse(perm_converter_, Role::kGetBlocks)(),
            2,
            query_hash);
      }

      auto ledger_height = block_store_.size();
      if (q.height() > ledger_height) {
        // invalid height
        return logAndReturnErrorResponse(
            QueryErrorType::kStatefulFailed,
            "requested height (" + std::to_string(q.height())
                + ") is greater than the ledger's one ("
                + std::to_string(ledger_height) + ")",
            3,
            query_hash);
      }

      auto block_deserialization_msg = [height = q.height()] {
        return "could not retrieve block with given height: "
            + std::to_string(height);
      };
      auto block = block_store_.fetch(q.height());
      if (not block) {
        // for some reason, block with such height was not retrieved
        return logAndReturnErrorResponse(QueryErrorType::kStatefulFailed,
                                         block_deserialization_msg(),
                                         1,
                                         query_hash);
      }
      return query_response_factory_->createBlockResponse(clone(**block),
                                                          query_hash);
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetSignatories &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple = QueryType<std::string>;
      using PermissionTuple = boost::tuple<int>;

      auto cmd = (boost::format(R"(WITH has_perms AS (%s),
      t AS (
          SELECT public_key FROM account_has_signatory
          WHERE account_id = :account_id
      )
      SELECT public_key, perm FROM t
      RIGHT OUTER JOIN has_perms ON TRUE
      )")
                  % hasQueryPermission(creator_id,
                                       q.accountId(),
                                       Role::kGetMySignatories,
                                       Role::kGetAllSignatories,
                                       Role::kGetDomainSignatories))
                     .str();

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] { return (sql_.prepare << cmd, soci::use(q.accountId())); },
          query_hash,
          [this, &q, &query_hash](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            if (range_without_nulls.empty()) {
              return this->logAndReturnErrorResponse(
                  QueryErrorType::kNoSignatories, q.accountId(), 0, query_hash);
            }

            auto pubkeys = boost::copy_range<
                std::vector<shared_model::interface::types::PubkeyType>>(
                range_without_nulls | boost::adaptors::transformed([](auto t) {
                  return iroha::ametsuchi::apply(t, [&](auto &public_key) {
                    return shared_model::interface::types::PubkeyType{
                        shared_model::crypto::Blob::fromHexString(public_key)};
                  });
                }));

            return query_response_factory_->createSignatoriesResponse(
                pubkeys, query_hash);
          },
          notEnoughPermissionsResponse(perm_converter_,
                                       Role::kGetMySignatories,
                                       Role::kGetAllSignatories,
                                       Role::kGetDomainSignatories));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetAccountTransactions &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      std::string related_txs = R"(SELECT DISTINCT height, index
      FROM tx_position_by_creator
      WHERE creator_id = :account_id
      ORDER BY height, index ASC)";

      const auto &pagination_info = q.paginationMeta();
      auto first_hash = pagination_info.firstTxHash();
      // retrieve one extra transaction to populate next_hash
      auto query_size = pagination_info.pageSize() + 1u;

      auto apply_query = [&](const auto &query) {
        return [&] {
          if (first_hash) {
            return (sql_.prepare << query,
                    soci::use(q.accountId()),
                    soci::use(first_hash->hex()),
                    soci::use(query_size));
          } else {
            return (sql_.prepare << query,
                    soci::use(q.accountId()),
                    soci::use(query_size));
          }
        };
      };

      auto check_query = [this](const auto &q) {
        if (this->existsInDb<int>(
                "account", "account_id", "quorum", q.accountId())) {
          return QueryFallbackCheckResult{};
        }
        return QueryFallbackCheckResult{
            5, "no account with such id found: " + q.accountId()};
      };

      return executeTransactionsQuery(q,
                                      creator_id,
                                      query_hash,
                                      std::move(check_query),
                                      related_txs,
                                      apply_query,
                                      Role::kGetMyAccTxs,
                                      Role::kGetAllAccTxs,
                                      Role::kGetDomainAccTxs);
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetTransactions &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      auto escape = [](auto &hash) { return "'" + hash.hex() + "'"; };
      std::string hash_str = std::accumulate(
          std::next(q.transactionHashes().begin()),
          q.transactionHashes().end(),
          escape(q.transactionHashes().front()),
          [&escape](auto &acc, auto &val) { return acc + "," + escape(val); });

      using QueryTuple =
          QueryType<shared_model::interface::types::HeightType, std::string>;
      using PermissionTuple = boost::tuple<int, int>;

      auto cmd =
          (boost::format(R"(WITH has_my_perm AS (%s),
      has_all_perm AS (%s),
      t AS (
          SELECT height, hash FROM position_by_hash WHERE hash IN (%s)
      )
      SELECT height, hash, has_my_perm.perm, has_all_perm.perm FROM t
      RIGHT OUTER JOIN has_my_perm ON TRUE
      RIGHT OUTER JOIN has_all_perm ON TRUE
      )") % getAccountRolePermissionCheckSql(Role::kGetMyTxs, ":account_id")
           % getAccountRolePermissionCheckSql(Role::kGetAllTxs, ":account_id")
           % hash_str)
              .str();

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd, soci::use(creator_id, "account_id"));
          },
          query_hash,
          [&](auto range, auto &my_perm, auto &all_perm) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            if (boost::size(range_without_nulls)
                != q.transactionHashes().size()) {
              // TODO [IR-1816] Akvinikym 03.12.18: replace magic number 4
              // with a named constant
              // at least one of the hashes in the query was invalid -
              // nonexistent or permissions were missed
              return this->logAndReturnErrorResponse(
                  QueryErrorType::kStatefulFailed,
                  "At least one of the supplied hashes is incorrect",
                  4,
                  query_hash);
            }
            std::map<uint64_t, std::unordered_set<std::string>> index;
            for (const auto &t : range_without_nulls) {
              iroha::ametsuchi::apply(t, [&index](auto &height, auto &hash) {
                index[height].insert(hash);
              });
            }

            std::vector<std::unique_ptr<shared_model::interface::Transaction>>
                response_txs;
            for (auto &block : index) {
              auto txs = this->getTransactionsFromBlock(
                  block.first,
                  [](auto size) {
                    return boost::irange(static_cast<decltype(size)>(0), size);
                  },
                  [&](auto &tx) {
                    return block.second.count(tx.hash().hex()) > 0
                        and (all_perm
                             or (my_perm
                                 and tx.creatorAccountId() == creator_id));
                  });
              std::move(
                  txs.begin(), txs.end(), std::back_inserter(response_txs));
            }

            return query_response_factory_->createTransactionsResponse(
                std::move(response_txs), query_hash);
          },
          notEnoughPermissionsResponse(
              perm_converter_, Role::kGetMyTxs, Role::kGetAllTxs));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetAccountAssetTransactions &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      std::string related_txs = R"(SELECT DISTINCT height, index
          FROM position_by_account_asset
          WHERE account_id = :account_id
          AND asset_id = :asset_id
          ORDER BY height, index ASC)";  // consider index when changing this

      const auto &pagination_info = q.paginationMeta();
      auto first_hash = pagination_info.firstTxHash();
      // retrieve one extra transaction to populate next_hash
      auto query_size = pagination_info.pageSize() + 1u;

      auto apply_query = [&](const auto &query) {
        return [&] {
          if (first_hash) {
            return (sql_.prepare << query,
                    soci::use(q.accountId()),
                    soci::use(q.assetId()),
                    soci::use(first_hash->hex()),
                    soci::use(query_size));
          } else {
            return (sql_.prepare << query,
                    soci::use(q.accountId()),
                    soci::use(q.assetId()),
                    soci::use(query_size));
          }
        };
      };

      auto check_query = [this](const auto &q) {
        if (not this->existsInDb<int>(
                "account", "account_id", "quorum", q.accountId())) {
          return QueryFallbackCheckResult{
              5, "no account with such id found: " + q.accountId()};
        }
        if (not this->existsInDb<int>(
                "asset", "asset_id", "precision", q.assetId())) {
          return QueryFallbackCheckResult{
              6, "no asset with such id found: " + q.assetId()};
        }

        return QueryFallbackCheckResult{};
      };

      return executeTransactionsQuery(q,
                                      creator_id,
                                      query_hash,
                                      std::move(check_query),
                                      related_txs,
                                      apply_query,
                                      Role::kGetMyAccAstTxs,
                                      Role::kGetAllAccAstTxs,
                                      Role::kGetDomainAccAstTxs);
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetAccountAssets &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple =
          QueryType<shared_model::interface::types::AccountIdType,
                    shared_model::interface::types::AssetIdType,
                    std::string,
                    size_t>;
      using PermissionTuple = boost::tuple<int>;

      // get the assets
      auto cmd = (boost::format(R"(
      with has_perms as (%s),
      all_data as (
          select row_number() over () rn, *
          from (
              select *
              from account_has_asset
              where account_id = :account_id
              order by asset_id
          ) t
      ),
      total_number as (
          select rn total_number
          from all_data
          order by rn desc
          limit 1
      ),
      page_start as (
          select rn
          from all_data
          where coalesce(asset_id = :first_asset_id, true)
          limit 1
      ),
      page_data as (
          select * from all_data, page_start, total_number
          where
              all_data.rn >= page_start.rn and
              coalesce( -- TODO remove after pagination is mandatory IR-516
                  all_data.rn < page_start.rn + :page_size,
                  true
              )
      )
      select account_id, asset_id, amount, total_number, perm
          from
              page_data
              right join has_perms on true
      )")
                  % hasQueryPermission(creator_id,
                                       q.accountId(),
                                       Role::kGetMyAccAst,
                                       Role::kGetAllAccAst,
                                       Role::kGetDomainAccAst))
                     .str();

      // These must stay alive while soci query is being done.
      const auto pagination_meta{q.paginationMeta()};
      const auto req_first_asset_id =
          pagination_meta | [](const auto &pagination_meta) {
            return boost::optional<std::string>(pagination_meta.firstAssetId());
          };
      const auto req_page_size =  // TODO 2019.05.31 mboldyrev make it
                                  // non-optional after IR-516
          pagination_meta | [](const auto &pagination_meta) {
            return boost::optional<size_t>(pagination_meta.pageSize() + 1);
          };

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd,
                    soci::use(q.accountId(), "account_id"),
                    soci::use(req_first_asset_id, "first_asset_id"),
                    soci::use(req_page_size, "page_size"));
          },
          query_hash,
          [&](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            std::vector<
                std::tuple<shared_model::interface::types::AccountIdType,
                           shared_model::interface::types::AssetIdType,
                           shared_model::interface::Amount>>
                assets;
            size_t total_number = 0;
            for (const auto &row : range_without_nulls) {
              iroha::ametsuchi::apply(
                  row,
                  [&assets, &total_number](auto &account_id,
                                           auto &asset_id,
                                           auto &amount,
                                           auto &total_number_col) {
                    total_number = total_number_col;
                    assets.push_back(std::make_tuple(
                        std::move(account_id),
                        std::move(asset_id),
                        shared_model::interface::Amount(amount)));
                  });
            }
            if (assets.empty() and req_first_asset_id) {
              // nonexistent first_asset_id provided in query request
              return this->logAndReturnErrorResponse(
                  QueryErrorType::kStatefulFailed,
                  q.accountId(),
                  4,
                  query_hash);
            }
            assert(total_number >= assets.size());
            const bool is_last_page = not q.paginationMeta()
                or (assets.size() <= q.paginationMeta()->pageSize());
            boost::optional<shared_model::interface::types::AssetIdType>
                next_asset_id;
            if (not is_last_page) {
              next_asset_id = std::get<1>(assets.back());
              assets.pop_back();
              assert(assets.size() == q.paginationMeta()->pageSize());
            }
            return query_response_factory_->createAccountAssetResponse(
                assets, total_number, next_asset_id, query_hash);
          },
          notEnoughPermissionsResponse(perm_converter_,
                                       Role::kGetMyAccAst,
                                       Role::kGetAllAccAst,
                                       Role::kGetDomainAccAst));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetAccountDetail &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple =
          QueryType<shared_model::interface::types::DetailType,
                    uint32_t,
                    shared_model::interface::types::AccountIdType,
                    shared_model::interface::types::AccountDetailKeyType,
                    uint32_t>;
      using PermissionTuple = boost::tuple<int>;

      auto cmd = (boost::format(R"(
      with has_perms as (%s),
      detail AS (
          with filtered_plain_data as (
              select row_number() over () rn, *
              from (
                  select
                      data_by_writer.key writer,
                      plain_data.key as key,
                      plain_data.value as value
                  from
                      jsonb_each((
                          select data
                          from account
                          where account_id = :account_id
                      )) data_by_writer,
                  jsonb_each(data_by_writer.value) plain_data
                  where
                      coalesce(data_by_writer.key = :writer, true) and
                      coalesce(plain_data.key = :key, true)
                  order by data_by_writer.key asc, plain_data.key asc
              ) t
          ),
          page_limits as (
              select start.rn as start, start.rn + :page_size as end
                  from (
                      select rn
                      from filtered_plain_data
                      where
                          coalesce(writer = :first_record_writer, true) and
                          coalesce(key = :first_record_key, true)
                      limit 1
                  ) start
          ),
          total_number as (select count(1) total_number from filtered_plain_data),
          next_record as (
              select writer, key
              from
                  filtered_plain_data,
                  page_limits
              where rn = page_limits.end
          ),
          page as (
              select json_object_agg(writer, data_by_writer) json
              from (
                  select writer, json_object_agg(key, value) data_by_writer
                  from
                      filtered_plain_data,
                      page_limits
                  where
                      rn >= page_limits.start and
                      coalesce(rn < page_limits.end, true)
                  group by writer
              ) t
          ),
          target_account_exists as (
            select count(1) val
            from account
            where account_id = :account_id
          )
          select
              page.json json,
              total_number,
              next_record.writer next_writer,
              next_record.key next_key,
              target_account_exists.val target_account_exists
          from
              page
              left join total_number on true
              left join next_record on true
              right join target_account_exists on true
      )
      select detail.*, perm from detail
      right join has_perms on true
      )")
                  % hasQueryPermission(creator_id,
                                       q.accountId(),
                                       Role::kGetMyAccDetail,
                                       Role::kGetAllAccDetail,
                                       Role::kGetDomainAccDetail))
                     .str();

      const auto writer = q.writer();
      const auto key = q.key();
      boost::optional<std::string> first_record_writer;
      boost::optional<std::string> first_record_key;
      boost::optional<size_t> page_size;
      // TODO 2019.05.29 mboldyrev IR-516 remove when pagination is made
      // mandatory
      q.paginationMeta() | [&](const auto &pagination_meta) {
        page_size = pagination_meta.pageSize();
        pagination_meta.firstRecordId() | [&](const auto &first_record_id) {
          first_record_writer = first_record_id.writer();
          first_record_key = first_record_id.key();
        };
      };

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd,
                    soci::use(q.accountId(), "account_id"),
                    soci::use(writer, "writer"),
                    soci::use(key, "key"),
                    soci::use(first_record_writer, "first_record_writer"),
                    soci::use(first_record_key, "first_record_key"),
                    soci::use(page_size, "page_size"));
          },
          query_hash,
          [&, this](auto range, auto &) {
            if (range.empty()) {
              assert(not range.empty());
              log_->error("Empty response range in {}.", q);
              return this->logAndReturnErrorResponse(
                  QueryErrorType::kNoAccountDetail,
                  q.accountId(),
                  0,
                  query_hash);
            }

            return iroha::ametsuchi::apply(
                range.front(),
                [&, this](auto &json,
                          auto &total_number,
                          auto &next_writer,
                          auto &next_key,
                          auto &target_account_exists) {
                  if (target_account_exists.value_or(0) == 0) {
                    // TODO 2019.06.11 mboldyrev IR-558 redesign missing data
                    // handling
                    return this->logAndReturnErrorResponse(
                        QueryErrorType::kNoAccountDetail,
                        q.accountId(),
                        0,
                        query_hash);
                  }
                  assert(target_account_exists.value() == 1);
                  if (json) {
                    BOOST_ASSERT_MSG(total_number, "Mandatory value missing!");
                    if (not total_number) {
                      this->log_->error(
                          "Mandatory total_number value is missing in "
                          "getAccountDetail query result {}.",
                          q);
                    }
                    boost::optional<shared_model::plain::AccountDetailRecordId>
                        next_record_id{[this, &next_writer, &next_key]()
                                           -> decltype(next_record_id) {
                          if (next_key or next_writer) {
                            if (not next_writer) {
                              log_->error(
                                  "next_writer not set for next_record_id!");
                              assert(next_writer);
                              return boost::none;
                            }
                            if (not next_key) {
                              log_->error(
                                  "next_key not set for next_record_id!");
                              assert(next_key);
                              return boost::none;
                            }
                            return shared_model::plain::AccountDetailRecordId{
                                next_writer.value(), next_key.value()};
                          }
                          return boost::none;
                        }()};
                    return query_response_factory_->createAccountDetailResponse(
                        json.value(),
                        total_number.value_or(0),
                        next_record_id |
                            [](const auto &next_record_id) {
                              return boost::optional<
                                  const shared_model::interface::
                                      AccountDetailRecordId &>(next_record_id);
                            },
                        query_hash);
                  }
                  if (total_number.value_or(0) > 0) {
                    // the only reason for it is nonexistent first record
                    assert(first_record_writer or first_record_key);
                    return this->logAndReturnErrorResponse(
                        QueryErrorType::kStatefulFailed,
                        q.accountId(),
                        4,
                        query_hash);
                  } else {
                    // no account details matching query
                    // TODO 2019.06.11 mboldyrev IR-558 redesign missing data
                    // handling
                    return query_response_factory_->createAccountDetailResponse(
                        kEmptyDetailsResponse, 0, boost::none, query_hash);
                  }
                });
          },
          notEnoughPermissionsResponse(perm_converter_,
                                       Role::kGetMyAccDetail,
                                       Role::kGetAllAccDetail,
                                       Role::kGetDomainAccDetail));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetRoles &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple = QueryType<shared_model::interface::types::RoleIdType>;
      using PermissionTuple = boost::tuple<int>;

      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s)
      SELECT role_id, perm FROM role
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % getAccountRolePermissionCheckSql(Role::kGetRoles))
                     .str();

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd,
                    soci::use(creator_id, "role_account_id"));
          },
          query_hash,
          [&](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            auto roles = boost::copy_range<
                std::vector<shared_model::interface::types::RoleIdType>>(
                range_without_nulls | boost::adaptors::transformed([](auto t) {
                  return iroha::ametsuchi::apply(
                      t, [](auto &role_id) { return role_id; });
                }));

            return query_response_factory_->createRolesResponse(roles,
                                                                query_hash);
          },
          notEnoughPermissionsResponse(perm_converter_, Role::kGetRoles));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetRolePermissions &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple = QueryType<std::string>;
      using PermissionTuple = boost::tuple<int>;

      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s),
      perms AS (SELECT permission FROM role_has_permissions
                WHERE role_id = :role_name)
      SELECT permission, perm FROM perms
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % getAccountRolePermissionCheckSql(Role::kGetRoles))
                     .str();

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd,
                    soci::use(creator_id, "role_account_id"),
                    soci::use(q.roleId(), "role_name"));
          },
          query_hash,
          [this, &q, &creator_id, &query_hash](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            if (range_without_nulls.empty()) {
              return this->logAndReturnErrorResponse(
                  QueryErrorType::kNoRoles,
                  "{" + q.roleId() + ", " + creator_id + "}",
                  0,
                  query_hash);
            }

            return iroha::ametsuchi::apply(
                range_without_nulls.front(),
                [this, &query_hash](auto &permission) {
                  return query_response_factory_->createRolePermissionsResponse(
                      shared_model::interface::RolePermissionSet(permission),
                      query_hash);
                });
          },
          notEnoughPermissionsResponse(perm_converter_, Role::kGetRoles));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetAssetInfo &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple =
          QueryType<shared_model::interface::types::DomainIdType, uint32_t>;
      using PermissionTuple = boost::tuple<int>;

      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s),
      perms AS (SELECT domain_id, precision FROM asset
                WHERE asset_id = :asset_id)
      SELECT domain_id, precision, perm FROM perms
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % getAccountRolePermissionCheckSql(Role::kReadAssets))
                     .str();

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd,
                    soci::use(creator_id, "role_account_id"),
                    soci::use(q.assetId(), "asset_id"));
          },
          query_hash,
          [this, &q, &creator_id, &query_hash](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            if (range_without_nulls.empty()) {
              return this->logAndReturnErrorResponse(
                  QueryErrorType::kNoAsset,
                  "{" + q.assetId() + ", " + creator_id + "}",
                  0,
                  query_hash);
            }

            return iroha::ametsuchi::apply(
                range_without_nulls.front(),
                [this, &q, &query_hash](auto &domain_id, auto &precision) {
                  return query_response_factory_->createAssetResponse(
                      q.assetId(), domain_id, precision, query_hash);
                });
          },
          notEnoughPermissionsResponse(perm_converter_, Role::kReadAssets));
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetPendingTransactions &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      std::vector<std::unique_ptr<shared_model::interface::Transaction>>
          response_txs;
      if (q.paginationMeta()) {
        return pending_txs_storage_
            ->getPendingTransactions(creator_id,
                                     q.paginationMeta()->pageSize(),
                                     q.paginationMeta()->firstTxHash())
            .match(
                [this, &response_txs, &query_hash](auto &&response) {
                  auto &interface_txs = response.value.transactions;
                  response_txs.reserve(interface_txs.size());
                  // TODO igor-egorov 2019-06-06 IR-555 avoid use of clone()
                  std::transform(interface_txs.begin(),
                                 interface_txs.end(),
                                 std::back_inserter(response_txs),
                                 [](auto &tx) { return clone(*tx); });
                  return query_response_factory_
                      ->createPendingTransactionsPageResponse(
                          std::move(response_txs),
                          response.value.all_transactions_size,
                          std::move(response.value.next_batch_info),
                          query_hash);
                },
                [this, &q, &query_hash](auto &&error) {
                  switch (error.error) {
                    case iroha::PendingTransactionStorage::ErrorCode::kNotFound:
                      return query_response_factory_->createErrorQueryResponse(
                          shared_model::interface::QueryResponseFactory::
                              ErrorQueryType::kStatefulFailed,
                          std::string("The batch with specified first "
                                      "transaction hash not found, the hash: ")
                              + q.paginationMeta()->firstTxHash()->toString(),
                          4,  // missing first tx hash error
                          query_hash);
                    default:
                      BOOST_ASSERT_MSG(false,
                                       "Unknown and unhandled type of error "
                                       "happend in pending txs storage");
                      return query_response_factory_->createErrorQueryResponse(
                          shared_model::interface::QueryResponseFactory::
                              ErrorQueryType::kStatefulFailed,
                          std::string("Unknown type of error happened: ")
                              + std::to_string(error.error),
                          1,  // unknown internal error
                          query_hash);
                  }
                });
      } else {  // TODO 2019-06-06 igor-egorov IR-516 remove deprecated
                // interface
        auto interface_txs =
            pending_txs_storage_->getPendingTransactions(creator_id);
        response_txs.reserve(interface_txs.size());

        std::transform(interface_txs.begin(),
                       interface_txs.end(),
                       std::back_inserter(response_txs),
                       [](auto &tx) { return clone(*tx); });
        return query_response_factory_->createTransactionsResponse(
            std::move(response_txs), query_hash);
      }
    }

    QueryExecutorResult PostgresSpecificQueryExecutor::operator()(
        const shared_model::interface::GetPeers &q,
        const shared_model::interface::types::AccountIdType &creator_id,
        const shared_model::interface::types::HashType &query_hash) {
      using QueryTuple =
          QueryType<std::string, shared_model::interface::types::AddressType>;
      using PermissionTuple = boost::tuple<int>;

      auto cmd = (boost::format(
                      R"(WITH has_perms AS (%s)
      SELECT public_key, address, perm FROM peer
      RIGHT OUTER JOIN has_perms ON TRUE
      )") % getAccountRolePermissionCheckSql(Role::kGetPeers))
                     .str();

      return executeQuery<QueryTuple, PermissionTuple>(
          [&] {
            return (sql_.prepare << cmd,
                    soci::use(creator_id, "role_account_id"));
          },
          query_hash,
          [&](auto range, auto &) {
            auto range_without_nulls = resultWithoutNulls(std::move(range));
            shared_model::interface::types::PeerList peers;
            for (const auto &row : range_without_nulls) {
              iroha::ametsuchi::apply(
                  row, [&peers](auto &peer_key, auto &address) {
                    peers.push_back(std::make_shared<shared_model::plain::Peer>(
                        address,
                        shared_model::interface::types::PubkeyType{
                            shared_model::crypto::Blob::fromHexString(
                                peer_key)}));
                  });
            }
            return query_response_factory_->createPeersResponse(peers,
                                                                query_hash);
          },
          notEnoughPermissionsResponse(perm_converter_, Role::kGetPeers));
    }

    template <typename ReturnValueType>
    bool PostgresSpecificQueryExecutor::existsInDb(
        const std::string &table_name,
        const std::string &key_name,
        const std::string &value_name,
        const std::string &value) const {
      auto cmd = (boost::format(R"(SELECT %s
                                   FROM %s
                                   WHERE %s = '%s'
                                   LIMIT 1)")
                  % value_name % table_name % key_name % value)
                     .str();
      soci::rowset<ReturnValueType> result = this->sql_.prepare << cmd;
      return result.begin() != result.end();
    }

  }  // namespace ametsuchi
}  // namespace iroha
