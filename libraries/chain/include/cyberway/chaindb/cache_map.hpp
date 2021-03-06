#pragma once

#include <cyberway/chaindb/cache_item.hpp>
#include <cyberway/chaindb/storage_payer_info.hpp>

namespace cyberway { namespace chaindb {

    class cache_map final {
    public:
        cache_map();
        ~cache_map();

        void set_cache_converter(const table_info&, const cache_converter_interface&) const;
        bool has_cache_converter(const table_info&) const;

        void set_next_pk(const table_info&, primary_key_t) const;

        cache_object_ptr create(const table_info&, primary_key_t, const storage_payer_info&) const;
        cache_object_ptr create(const table_info&, const storage_payer_info&) const;

        void destroy(cache_object& obj) const;

        cache_object_ptr find(const service_state&) const;
        cache_object_ptr find(const service_state&, index_name_t, const char*, size_t) const;

        cache_object_ptr emplace(const table_info&, object_value) const;
        void remove(const table_info&, primary_key_t) const;

        void emplace_unsuccess(const table_info&, primary_key_t, primary_key_t) const;
        void emplace_unsuccess(const index_info&, const char*, size_t, primary_key_t) const;

        cache_object_ptr find_unsuccess(const service_state&) const;
        cache_object_ptr find_unsuccess(const service_state&, index_name_t, const char*, size_t) const;

        void clear_unsuccess(const table_info&) const;

        void set_value(const table_info&, cache_object&, const object_value&) const;
        void set_object(const table_info&, cache_object&, object_value) const;
        void set_service(const table_info&, cache_object&, service_state) const;
        void set_revision(const object_value&, revision_t) const;
        void set_subjective_ram(uint64_t size, uint64_t reserved_size, uint32_t rlm) const;

        uint64_t calc_ram_bytes(revision_t) const;
        void set_revision(revision_t) const;
        void start_session(revision_t) const;
        void push_session(revision_t) const;
        void squash_session(revision_t) const;
        void undo_session(revision_t) const;

        void clear() const;
        void push(revision_t) const;

    private:
        std::unique_ptr<cache_map_impl> impl_;
    }; // class cache_map

} } // namespace cyberway::chaindb
