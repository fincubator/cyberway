#pragma once

#include <cyberway/chaindb/common.hpp>

namespace cyberway { namespace chaindb {

    const string& get_system_code_name();
    const string& get_unknown_name();
    const string& get_scope_field_name();
    const string& get_payer_field_name();
    const string& get_size_field_name();

    ///----

    inline string get_code_name(const account_name& code) {
        if (!code.empty()) {
            return code.to_string();
        }
        return get_system_code_name();
    }

    inline string get_code_name(const account_name_t code) {
        return get_code_name(account_name(code));
    }

    template <typename Info>
    inline string get_code_name(const Info& info) {
        return get_code_name(info.code);
    }

    ///----

    inline string get_scope_name(const account_name& scope) {
        return get_code_name(scope);
    }

    template <typename Info>
    inline string get_scope_name(const Info& info) {
        return get_scope_name(info.scope);
    }

    ///---

    inline string get_payer_name(const account_name& payer) {
        return payer.to_string();
    }

    ///----

    inline string get_table_name(const table_name& table) {
        if (!table.empty()) {
            return table.to_string();
        }
        return get_unknown_name();
    }

    inline string get_table_name(const table_name_t table) {
        return get_table_name(eosio::chain::table_name(table));
    }

    inline string get_table_name(const table_def* table) {
        if (table) {
            return get_table_name(table->name);
        }
        return get_unknown_name();
    }

    template <typename Info>
    inline string get_table_name(const Info& info) {
        return get_table_name(info.table);
    }

    template<typename Info>
    inline string get_full_table_name(const Info& info) {
        return get_code_name(info).append(".").append(get_table_name(info));
    }

    ///-----

    inline string get_index_name(const index_name& index) {
        if (!index.empty()) {
            return index.to_string();
        }
        return get_unknown_name();
    }

    inline string get_index_name(const index_name_t index) {
        return get_index_name(eosio::chain::index_name(index));
    }

    template <typename Info>
    inline string get_index_name(const Info& info) {
        return get_index_name(info.index);
    }

    inline string get_index_name(const index_def& index) {
        return get_index_name(index.name);
    }

    inline string get_index_name(const index_def* index) {
        if (index) {
            return get_index_name(*index);
        }
        return get_unknown_name();
    }

    template<typename Info>
    inline string get_full_index_name(const Info& info) {
        return get_full_table_name(info).append(".").append(get_index_name(info.index));
    }
} } // namespace cyberway::chaindb