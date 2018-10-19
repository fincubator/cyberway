#pragma once

#include <cyberway/chaindb/driver_interface.hpp>

namespace cyberway { namespace chaindb {

    class undo_stack final {
    public:
        undo_stack(driver_interface&);

        undo_stack(const undo_stack&) = delete;
        undo_stack(undo_stack&&) = delete;

        ~undo_stack();

        chaindb_session start_undo_session(bool enabled);

        void set_revision(int64_t revision);
        int64_t revision() const;

        /** leaves the UNDO state on the stack when session goes out of scope */
        void push(int64_t revision);

        /**
         *  Restores the state to how it was prior to the current session discarding all changes
         *  made between the last revision and the current revision.
         */
        void undo(int64_t revision);

        /**
         *  This method works similar to git squash, it merges the change set from the two most
         *  recent revision numbers into one revision number (reducing the head revision number)
         *
         *  This method does not change the state of the index, only the state of the undo buffer.
         */
        void squash(int64_t revision);

        /**
         * Discards all undo history prior to revision
         */
        void commit(int64_t revision);

        /**
         * Unwinds all undo states
         */
        void undo_all();

        /**
         * Event on create objects
         */
        void insert(const table_info&, primary_key_t pk, variant value);

        /**
         * Event on modify objects
         */
        void update(const table_info&, primary_key_t pk, variant value);

        /**
         * Event on remove objects
         */
        void remove(const table_info&, primary_key_t pk, variant value);

    private:
        struct undo_stack_impl_;
        std::unique_ptr<undo_stack_impl_> impl_;
    }; // class table_undo_stack

} } // namespace cyberway::chaindb