#include <assert.h>
#include <atomic>
#include <stdint.h>

#pragma once

/**
 * The class implements a storage for single time-read after the latest write.
 * This class is non-blocking and thread safe.
 * It's assumed that there's only one Producer and a single Consumer.
 */
template <typename T>
class Disposable {
public:
    using Type = T;
    using Self = Disposable<Type>;

    /**
     * Lock class. Implements RAII if required.
     * May be used in a way similar to unique_lock also.
     */
    class ReadLock {
    private:
        friend Self;

        using PtrT = const Type *;
        using RefT = const Type &;

        Self &_host;

        PtrT _ptr;

        ReadLock(Self &h, bool try_lock = false) : _host{h}, _ptr{nullptr}
        {
            if (try_lock) {
                this->try_lock();
            }
        }

    public:
        ~ReadLock() { unlock(); }

        bool try_lock() {
            if (_host._try_block_for_read()) {
                _ptr = &_host._storage;
            }

            return _ptr;
        }

        void unlock() {
            if (_ptr) {
                _host._unblock_after_read_and_empty_storage();
                _ptr = nullptr;
            }
        }

        bool is_locked() const { return _ptr; }
        PtrT read() const { return _ptr; }
        operator PtrT () const { return read(); }
        operator RefT () const { return *read(); }
        operator bool() const { return is_locked(); }
    };

    Disposable() : _state{STATE_STORAGE_EMPTY_MASK} {}

    // Returns an unlocked version of read lock
    ReadLock get_lock() {
        return ReadLock{*this};
    }

    /**
     * Try to acquire read lock
     * \returns instance of ReadLock class
     */
    ReadLock try_lock() {
        return ReadLock{*this, true};
    }

    /**
     * Non-blocking read and copy.
     * The storage becomes empty on successfull read.
     *
     * \param ret target memory location to copy into
     * \returns \c true if copy was successfull, \c false if the read was blocked by simultaneous write or the storage was empty.
     */
    bool try_read_into(T &ret) {
        if (_try_block_for_read()) {
            ret = _storage;

//             std::atomic_thread_fence(std::memory_order_release);
            _unblock_after_read_and_empty_storage();

            return true;
        }

        return false;
    }

    /**
     * Non-blocking write.
     *
     * \param v value to store
     * \returns \c true if write was successfull, \c false if the operation was blocked by simultaneous read
     */
    bool try_put(const T &v) {
        if (_try_block_for_write()) {
            _storage = v;

//             std::atomic_thread_fence(std::memory_order_acquire);
            _unblock_after_write_and_fill_storage();

            return true;
        }

        return false;
    }

protected:
    using StateType = uint16_t;

    Type _storage;

    std::atomic<StateType> _state;

    static constexpr StateType STATE_STORAGE_EMPTY_MASK = 1;
    static constexpr StateType STATE_READ_BLOCK_MASK = 2;
    static constexpr StateType STATE_WRITE_BLOCK_MASK = 4;

    inline StateType _clear_state_mask(StateType orig, StateType mask) {
        return orig & (~mask);
    }

    inline StateType _set_state_mask(StateType orig, StateType mask) {
        return orig | mask;
    }

    // block for read if and only if the storage isn't empty and there's no write operation taking place at the moment
    bool _try_block_for_read() {
        auto expected = _state.load();
        expected = _clear_state_mask(expected, STATE_WRITE_BLOCK_MASK | STATE_STORAGE_EMPTY_MASK);
        const auto desired = _set_state_mask(expected, STATE_READ_BLOCK_MASK);

        return _state.compare_exchange_strong(expected, desired);
    }

    // should only be called after a successfull _try_block_for_read
    void _unblock_after_read_and_empty_storage() {
        auto expected = _state.load();

        assert(((expected ^ STATE_READ_BLOCK_MASK) == 0) && "invalid state on unblock after read");

        auto desired = _clear_state_mask(expected, STATE_READ_BLOCK_MASK);
        desired = _set_state_mask(desired, STATE_STORAGE_EMPTY_MASK);

        bool rc = _state.compare_exchange_strong(expected, desired);

        assert(rc && "unblock after read failed");
    }

    // block for write if and only if the storage isn't blocked for read
    bool _try_block_for_write() {
        auto expected = _clear_state_mask(_state.load(), STATE_READ_BLOCK_MASK);
        const auto desired = _set_state_mask(expected, STATE_WRITE_BLOCK_MASK);

        return _state.compare_exchange_strong(expected, desired);
    }

    // called only after successful _try_block_for_write
    void _unblock_after_write_and_fill_storage() {
        auto expected = _state.load();

        assert(((expected & STATE_WRITE_BLOCK_MASK) && !(expected & STATE_READ_BLOCK_MASK)) && "invalid state on unblock after write");

        const auto desired = _clear_state_mask(expected, STATE_WRITE_BLOCK_MASK | STATE_STORAGE_EMPTY_MASK);

        bool rc = _state.compare_exchange_strong(expected, desired);

        assert(rc && "unblock after write failed");
    }
};
