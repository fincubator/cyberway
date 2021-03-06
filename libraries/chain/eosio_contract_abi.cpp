#include <eosio/chain/abi_def.hpp>
#include <fc/utility.hpp>

#include <eosio/chain/account_object.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/block_summary_object.hpp>
#include <eosio/chain/transaction_object.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/stake_object.hpp>
#include <eosio/chain/permission_object.hpp>
#include <eosio/chain/permission_link_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>

#include <cyberway/chain/domain_object.hpp>

namespace eosio { namespace chain {

vector<type_def> common_type_defs() {
   vector<type_def> types;

   types.push_back( type_def{"account_name", "name"} );
   types.push_back( type_def{"permission_name", "name"} );
   types.push_back( type_def{"action_name", "name"} );
   types.push_back( type_def{"table_name", "name"} );
   types.push_back( type_def{"transaction_id_type", "checksum256"} );
   types.push_back( type_def{"block_id_type", "checksum256"} );
   types.push_back( type_def{"weight_type", "uint16"} );

   return types;
}

void set_common_defs(abi_def& abi) {
    if (abi.version.size() == 0) {
        abi.version = "cyberway::abi/1.0";
    }
    fc::move_append(abi.types, common_type_defs());
}


abi_def eosio_contract_abi(abi_def eos_abi)
{
   if( eos_abi.version.size() == 0 ) {
      eos_abi.version = "cyberway::abi/1.0";
   }

   fc::move_append(eos_abi.types, common_type_defs());

   // transaction
   eos_abi.structs.emplace_back( struct_def {
      "permission_level", "", {
         {"actor", "account_name"},
         {"permission", "permission_name"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "action", "", {
         {"account", "account_name"},
         {"name", "action_name"},
         {"authorization", "permission_level[]"},
         {"data", "bytes"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "extension", "", {
         {"type", "uint16"},
         {"data", "bytes"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "transaction_header", "", {
         {"expiration", "time_point_sec"},
         {"ref_block_num", "uint16"},
         {"ref_block_prefix", "uint32"},
         {"max_net_usage_words", "varuint32"},
         {"max_cpu_usage_ms", "uint8"},
         {"max_ram_kbytes", "varuint32"},
         {"max_storage_kbytes", "varuint32"},
         {"delay_sec", "varuint32"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "transaction", "transaction_header", {
         {"context_free_actions", "action[]"},
         {"actions", "action[]"},
         {"transaction_extensions", "extension[]"}
      }
   });

   // block_header

   eos_abi.structs.emplace_back( struct_def {
      "producer_key", "", {
         {"producer_name", "account_name"},
         {"block_signing_key", "public_key"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "producer_schedule", "", {
         {"version", "uint32"},
         {"producers", "producer_key[]"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "block_header", "", {
         {"timestamp", "uint32"},
         {"producer", "account_name"},
         {"confirmed", "uint16"},
         {"previous", "block_id_type"},
         {"transaction_mroot", "checksum256"},
         {"action_mroot", "checksum256"},
         {"schedule_version", "uint32"},
         {"new_producers", "producer_schedule?"},
         {"header_extensions", "extension[]"}
      }
   });

   // authority
   eos_abi.structs.emplace_back( struct_def {
      "key_weight", "", {
         {"key", "public_key"},
         {"weight", "weight_type"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "permission_level_weight", "", {
         {"permission", "permission_level"},
         {"weight", "weight_type"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "wait_weight", "", {
         {"wait_sec", "uint32"},
         {"weight", "weight_type"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "authority", "", {
         {"threshold", "uint32"},
         {"keys", "key_weight[]"},
         {"accounts", "permission_level_weight[]"},
         {"waits", "wait_weight[]"}
      }
   });

   //
   // SYSTEM TABLES
   //

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "account_object", "", {
         {"name", "name"},
         {"vm_type", "uint8"},
         {"vm_version", "uint8"},
         {"privileged", "bool"},
         {"last_code_update", "time_point"},
         {"code_version", "checksum256"},
         {"abi_version", "checksum256"},
         {"creation_date", "block_timestamp_type"},
         {"code", "string"},
         {"abi", "bytes"}
      }
    });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<account_object>::get_code(), "account_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"name", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "account_sequence_object", "", {
         {"name", "name"},
         {"recv_sequence", "uint64"},
         {"auth_sequence", "uint64"},
         {"code_sequence", "uint64"},
         {"abi_sequence", "uint64"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<account_sequence_object>::get_code(), "account_sequence_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"name", "asc"}}}
      }
   });
   
   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "chain_config", "", {
         {"base_per_transaction_net_usage", "uint32"},
         {"context_free_discount_net_usage_num", "uint32"},
         {"context_free_discount_net_usage_den", "uint32"},
         {"min_transaction_cpu_usage", "uint32"},
         {"min_transaction_ram_usage", "uint64"},
         {"max_transaction_lifetime", "uint32"},
         {"deferred_trx_expiration_window", "uint32"},
         {"max_transaction_delay", "uint32"},
         {"max_inline_action_size", "uint32"},
         {"max_inline_action_depth", "uint16"},
         {"max_authority_depth", "uint16"},
         {"ram_size", "uint64"},
         {"reserved_ram_size", "uint64"},
         {"max_block_usage", "uint64[]"},
         {"max_transaction_usage", "uint64[]"},
         {"target_virtual_limits", "uint64[]"},
         {"min_virtual_limits", "uint64[]"},
         {"max_virtual_limits", "uint64[]"},
         {"usage_windows", "uint32[]"},
         {"virtual_limit_decrease_pct", "uint16[]"},
         {"virtual_limit_increase_pct", "uint16[]"},
         {"account_usage_windows", "uint32[]"}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "global_property_object", "", {
         {"id", "uint64"},
         {"proposed_schedule_block_num", "uint32?"},
         {"proposed_schedule", "producer_schedule"},
         {"configuration", "chain_config"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<global_property_object>::get_code(), "global_property_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "dynamic_global_property_object", "", {
         {"id", "uint64"},
         {"global_action_seq", "uint64"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<dynamic_global_property_object>::get_code(), "dynamic_global_property_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "block_summary_object", "", {
         {"id", "uint64"},
         {"block_id", "checksum256"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<block_summary_object>::get_code(), "block_summary_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "transaction_object", "", {
         {"id", "uint64"},
         {"expiration", "time_point_sec"},
         {"trx_id", "checksum256"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<transaction_object>::get_code(), "transaction_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<by_trx_id>::get_code(), true, {{"trx_id", "asc"}}},
         {cyberway::chaindb::tag<by_expiration>::get_code(), true, {{"expiration","asc"}, {"id","asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "generated_transaction_object", "", {
         {"id", "uint64"},
         {"trx_id", "checksum256"},
         {"sender", "name"},
         {"sender_id", "uint128"},
         {"delay_until", "time_point"},
         {"expiration", "time_point"},
         {"published", "time_point"},
         {"packed_trx", "string"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<generated_transaction_object>::get_code(), "generated_transaction_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<by_trx_id>::get_code(), true, {{"trx_id", "asc"}}},
         {cyberway::chaindb::tag<by_expiration>::get_code(), true, {{"expiration","asc"}, {"id","asc"}}},
         {cyberway::chaindb::tag<by_delay>::get_code(), true, {{"delay_until", "asc"}, {"id", "asc"}}},
         {cyberway::chaindb::tag<by_sender_id>::get_code(), true, {{"sender", "asc"}, {"sender_id", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back(eosio::chain::struct_def{
      "domain_object", "", {
         {"id", "uint64"},
         {"owner", "name"},
         {"linked_to", "name"},
         {"creation_date", "block_timestamp_type"},
         {"name", "string"}
      }
   });

   eos_abi.tables.emplace_back(eosio::chain::table_def{
      cyberway::chaindb::tag<cyberway::chain::domain_object>::get_code(), "domain_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<by_name>::get_code(), true, {{"name", "asc"}}},
         {cyberway::chaindb::tag<by_owner>::get_code(), true, {{"owner", "asc"},{"name", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back(eosio::chain::struct_def{
      "username_object", "", {
         {"id", "uint64"},
         {"owner", "name"},
         {"scope", "name"},
         {"name", "string"}
      }
   });

   eos_abi.tables.emplace_back(eosio::chain::table_def{
      cyberway::chaindb::tag<cyberway::chain::username_object>::get_code(), "username_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<by_scope_name>::get_code(), true, {{"scope", "asc"},{"name", "asc"}}},
         {cyberway::chaindb::tag<by_owner>::get_code(), true, {{"owner","asc"},{"scope","asc"},{"name","asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "permission_usage_object", "", {
         {"id", "uint64"},
         {"last_used", "time_point"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<permission_usage_object>::get_code(), "permission_usage_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "shared_authority", "", {
         {"threshold", "uint32"},
         {"keys", "key_weight[]"},
         {"accounts", "permission_level_weight[]"},
         {"waits", "wait_weight[]"}}
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "permission_object", "", {
         {"id", "uint64"},
         {"usage_id", "uint64"},
         {"parent", "uint64"},
         {"owner", "name"},
         {"name", "name"},
         {"last_updated", "time_point"},

         {"auth", "shared_authority"}}
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<permission_object>::get_code(), "permission_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<by_parent>::get_code(), true, {{"parent", "asc"}, {"id","asc"}}},
         {cyberway::chaindb::tag<by_owner>::get_code(), true, {{"owner","asc"}, {"name","asc"}}},
         {cyberway::chaindb::tag<by_name>::get_code(), true, {{"name","asc"}, {"id","asc"}}},
      }
   });

   eos_abi.structs.emplace_back (eosio::chain::struct_def{
      "permission_link_object", "", {
         {"id", "uint64"},
         {"account","name"},
         {"code","name"},
         {"message_type","name"},
         {"required_permission","name"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<permission_link_object>::get_code(), "permission_link_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<by_action_name>::get_code(), true, {
            {"account","asc"},
            {"code", "asc"},
            {"message_type","asc"}
         }},
         {cyberway::chaindb::tag<by_permission_name>::get_code(), true, {
            {"account","asc"},
            {"required_permission","asc"},
            {"code","asc"},
            {"message_type","asc"}
         }},
      }
   });

   namespace rl = resource_limits;

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "usage_accumulator", "", {
         {"last_ordinal", "uint32"},
         {"value_ex", "uint64"},
         {"consumed", "uint64"}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "resource_usage_object", "", {
         {"owner", "name"},
         {"accumulators", "usage_accumulator[]"}
      }
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<rl::resource_usage_object>::get_code(), "resource_usage_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"owner", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "ratio64", "", {
         {"numerator", "uint64"},
         {"denominator", "uint64"}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "elastic_limit_params", "", {
         {"target", "uint64"},
         {"min", "uint64"},
         {"max", "uint64"},
         {"periods", "uint32"},
         {"decrease_rate", "ratio64"},
         {"increase_rate", "ratio64"}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "resource_limits_config_object", "", {
         {"id", "uint64"},
         {"limit_parameters", "elastic_limit_params[]"},
         {"account_usage_average_windows", "uint32[]"}}
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<rl::resource_limits_config_object>::get_code(), "resource_limits_config_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def{
      "resource_limits_state_object", "", {
         {"id", "uint64"},
         {"block_usage_accumulators", "usage_accumulator[]"},
         {"pending_usage", "int64[]"},
         {"virtual_limits", "uint64[]"}}
   });

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<rl::resource_limits_state_object>::get_code(), "resource_limits_state_object", {
          {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def {
     "stake_agent_object", "",{
        {"id", "uint64"},
        {"token_code", "symbol_code"},
        {"account", "name"},
        {"proxy_level", "uint8"},
        {"last_proxied_update", "time_point_sec"},
        {"balance", "int64"},
        {"proxied", "int64" },
        {"shares_sum", "int64"},
        {"own_share",  "int64"},
        {"fee", "int16"},
        {"min_own_staked", "int64"},
        {"provided", "int64"},
        {"received", "int64"}}});

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<stake_agent_object>::get_code(), "stake_agent_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<stake_agent_object::by_key>::get_code(), true, {{"token_code", "asc"},{"account", "asc"}}}
      }
   });
   
   eos_abi.structs.emplace_back( eosio::chain::struct_def {
     "stake_candidate_object", "",{
        {"id", "uint64"},
        {"token_code", "symbol_code"},
        {"account", "name"},
        {"latest_pick", "time_point_sec"},
        {"votes", "int64"},
        {"priority", "int64"},
        {"signing_key", "public_key"},
        {"enabled", "bool"}}});
        
   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<stake_candidate_object>::get_code(), "stake_candidate_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<stake_candidate_object::by_key>::get_code(), true, {{"token_code", "asc"},{"account", "asc"}}},
         {cyberway::chaindb::tag<stake_candidate_object::by_votes>::get_code(), true, 
             {{"token_code", "asc"},{"enabled", "asc"},{"votes", "desc"},{"account", "asc"}}},
         {cyberway::chaindb::tag<stake_candidate_object::by_prior>::get_code(), true, 
             {{"token_code", "asc"},{"enabled", "asc"},{"priority", "asc"},{"votes", "desc"},{"account", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def {
      "stake_grant_object", "",{
        {"id", "uint64"},
        {"token_code", "symbol_code"},
        {"grantor_name", "name"},
        {"recipient_name", "name"},
        {"pct", "int16"},
        {"share", "int64"},
        {"break_fee", "int16"},
        {"break_min_own_staked", "int64" }}});

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<stake_grant_object>::get_code(), "stake_grant_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}},
         {cyberway::chaindb::tag<stake_agent_object::by_key>::get_code(), true,
             {{"token_code", "asc"},{"grantor_name", "asc"},{"recipient_name", "asc"}}}
      }
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def {
      "stake_param_object", "",{
        {"id", "uint64"},
        {"token_symbol", "symbol"},
        {"max_proxies", "uint8[]"},
        {"depriving_window", "int64"},
        {"min_own_staked_for_election", "int64"}}});

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<stake_param_object>::get_code(), "stake_param_object", {  // Maybe add "stake_" prefix? it's in cyber.abi namespace
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}}
   });

   eos_abi.structs.emplace_back( eosio::chain::struct_def {
      "stake_stat_object", "",{
        {"id", "uint64"},
        {"token_code", "symbol_code"},
        {"total_staked", "int64"},
        {"total_votes", "int64"},
        {"last_reward", "time_point_sec"},
        {"enabled", "bool"}}});

   eos_abi.tables.emplace_back( eosio::chain::table_def {
      cyberway::chaindb::tag<stake_stat_object>::get_code(), "stake_stat_object", {
         {cyberway::chaindb::tag<by_id>::get_code(), true, {{"id", "asc"}}}
         }
   });

   eos_abi.structs.emplace_back(struct_def{
      "_SERVICE_", "", {
         {"upk", "uint64"},
         {"rev", "int64"}}
   });

   eos_abi.structs.emplace_back(struct_def{
        "undo", "",
        {{"_SERVICE_", "_SERVICE_"}}
   });

   eos_abi.tables.emplace_back(table_def{
       "undo", "undo", "uint64", {
           {cyberway::chaindb::tag<by_id>::get_code(), true, {{"_SERVICE_.upk", "asc"}}}
       },
   });

    ////////////////////////
   // ACTION PAYLOADS

   eos_abi.structs.emplace_back( struct_def {
      "newaccount", "", {
         {"creator", "account_name"},
         {"name", "account_name"},
         {"owner", "authority"},
         {"active", "authority"},
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "setcode", "", {
         {"account", "account_name"},
         {"vmtype", "uint8"},
         {"vmversion", "uint8"},
         {"code", "bytes"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "setabi", "", {
         {"account", "account_name"},
         {"abi",     "bytes"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "updateauth", "", {
         {"account", "account_name"},
         {"permission", "permission_name"},
         {"parent", "permission_name"},
         {"auth", "authority"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "deleteauth", "", {
         {"account", "account_name"},
         {"permission", "permission_name"},
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "linkauth", "", {
         {"account", "account_name"},
         {"code", "account_name"},
         {"type", "action_name"},
         {"requirement", "permission_name"},
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "unlinkauth", "", {
         {"account", "account_name"},
         {"code", "account_name"},
         {"type", "action_name"},
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "providebw", "", {
         {"provider", "account_name"},
         {"account", "account_name"},
      }
   });
// TODO: requestbw
//   eos_abi.structs.emplace_back( struct_def {
//      "requestbw", "", {
//         {"provider", "account_name"},
//         {"account", "account_name"},
//      }
//   });

   eos_abi.structs.emplace_back( struct_def {
      "canceldelay", "", {
         {"canceling_auth", "permission_level"},
         {"trx_id", "transaction_id_type"},
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "onerror", "", {
         {"sender_id", "uint128"},
         {"sent_trx",  "bytes"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "onblock", "", {
         {"header", "block_header"}
      }
   });

   eos_abi.structs.emplace_back( struct_def {
      "setparams", "", {
         {"params",      "chain_config"}
      }
   });

   eos_abi.actions.push_back( action_def{name("newaccount"), "newaccount"} );
   eos_abi.actions.push_back( action_def{name("setcode"), "setcode"} );
   eos_abi.actions.push_back( action_def{name("setabi"), "setabi"} );
   eos_abi.actions.push_back( action_def{name("updateauth"), "updateauth"} );
   eos_abi.actions.push_back( action_def{name("deleteauth"), "deleteauth"} );
   eos_abi.actions.push_back( action_def{name("linkauth"), "linkauth"} );
   eos_abi.actions.push_back( action_def{name("unlinkauth"), "unlinkauth"} );
   eos_abi.actions.push_back( action_def{name("providebw"), "providebw"} );
// TODO: requestbw
//   eos_abi.actions.push_back( action_def{name("requestbw"), "requestbw"} );
   eos_abi.actions.push_back( action_def{name("canceldelay"), "canceldelay"} );
   eos_abi.actions.push_back( action_def{name("onerror"), "onerror"} );
   eos_abi.actions.push_back( action_def{name("onblock"), "onblock"} );

   eos_abi.actions.push_back( action_def{name("setparams"), "setparams"} );

   return eos_abi;
}


abi_def domain_contract_abi(abi_def abi) {
    set_common_defs(abi);
    abi.structs.emplace_back(struct_def {"newusername", "", {
        {"creator", "name"},
        {"owner",   "name"},
        {"name",    "string"}}
    });
    abi.structs.emplace_back(struct_def {"newdomain", "", {
        {"creator", "name"},
        {"name",    "string"}}
    });
    abi.structs.emplace_back(struct_def {"passdomain", "", {
        {"from", "name"},
        {"to",   "account_name"},
        {"name", "string"}}
    });
    abi.structs.emplace_back(struct_def {"linkdomain", "", {
        {"owner", "name"},
        {"to",    "name"},
        {"name",  "string"}}
    });
    abi.structs.emplace_back(struct_def {"unlinkdomain", "", {
        {"owner", "name"},
        {"name",  "string"}}
    });

    abi.actions.push_back(action_def{name("newusername"), "newusername"});
    abi.actions.push_back(action_def{name("newdomain"), "newdomain"});
    abi.actions.push_back(action_def{name("passdomain"), "passdomain"});
    abi.actions.push_back(action_def{name("linkdomain"), "linkdomain"});
    abi.actions.push_back(action_def{name("unlinkdomain"), "unlinkdomain"});

    return abi;
}

abi_def history_contract_abi(abi_def abi) {
    if( abi.version.size() == 0 ) {
       abi.version = "cyberway::abi/1.0";
    }

    fc::move_append(abi.types, common_type_defs());

    abi.structs.emplace_back(struct_def {"acchistory", "", {
        {"id", "uint64"},
        {"account", "account_name"},
        {"action_sequence_num", "uint64"},
        {"account_sequence_num", "int32"}}
    });

    abi.tables.emplace_back( eosio::chain::table_def {
       name("acchistory"), "acchistory", {
          { name("primary"), true, {
                {"id", "asc"}
            }
          }, {
            name("accactionseq"), false, {
               {"account", "asc"},
               {"account_sequence_num", "asc"},
            }
          },
       }
    });

    abi.structs.emplace_back(struct_def {"acthistory", "", {
        {"id", "uint64"},
        {"action_sequence_num", "uint64"},
        {"packed_action_trace", "string"},
        {"block_num", "int32"},
        {"block_time", "block_timestamp_type"},
        {"trx_id", "transaction_id_type"}}
    });

    abi.tables.emplace_back( eosio::chain::table_def {
       name("acthistory"), "acthistory", {
          { name("primary"), true, {
                {"id", "asc"}
            }
          }, {
            name("byactseqnum"), false, {
                {"action_sequence_num", "asc"}
            }
          }, {
            name("trxid"), false, {
                {"trx_id", "asc"},
                {"action_sequence_num", "asc"}
            }
          }
       }
    });

    abi.structs.emplace_back(struct_def {"pubkeyhist", "", {
        {"id", "uint64"},
        {"public_key", "public_key"},
        {"name",       "account_name"},
        {"permission", "permission_name"}}
    });

    abi.tables.emplace_back( eosio::chain::table_def {
       name("pubkeyhist"), "pubkeyhist", {
          { name("primary"), true, {{"id", "asc"}} },
          { name("bypubkey"), true, {
                {"public_key", "asc"},
                {"id", "asc"}
            }
          },
          { name("byaccperm"), true, {
                {"name", "asc"},
                {"permission", "asc"},
                {"id", "asc"}
            }
          }
       }
    });

    abi.structs.emplace_back(struct_def {"ctrlhistory", "", {
        {"id", "uint64"},
        {"controlled_account",    "account_name"},
        {"controlled_permission", "permission_name"},
        {"controlling_account",   "account_name"}}
    });

    abi.tables.emplace_back( eosio::chain::table_def {
       name("ctrlhistory"), "ctrlhistory", {
          { name("primary"), true, {
                {"id", "asc"}
            }
          },
          { name("bycontrol"), true, {
                {"controlling_account", "asc"},
                {"id", "asc"}
            }
          },
          { name("controlauth"), false, {
                {"controlled_account", "asc"},
                {"controlled_permission", "asc"},
                {"controlling_account", "asc"}
            }
          }
       }
    });


    return abi;
}

} } /// eosio::chain
