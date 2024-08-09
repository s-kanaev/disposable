#include "disposable.h"

#include <iostream>
#include <thread>
#include <stdio.h>

static constexpr size_t SIZE = 100;
struct Data {
    unsigned long long v[SIZE];
};

Disposable<Data> disposable;

void prepare_data(Data &d, unsigned long long idx) {
   for (size_t i = 0; i < SIZE; ++i) {
      d.v[i] = idx;
    }
}

void Producer(std::atomic<bool> &cont) {
  unsigned long long idx = 1;

  Data d;

  while (cont.load()) {
    printf("[G] Idx = %llu\n", idx);
    prepare_data(d, idx);

//     printf("[G] W\n");
    disposable.try_put(d);

    ++idx;
    std::this_thread::yield();
  }
}

void Consumer(std::atomic<bool> &cont) {
  unsigned long long idx = 0;
  unsigned long long idx2 = std::numeric_limits<unsigned long long>::max();
  int pause_count = 0;
  const int pause_count_limit = 100;
  Data d;
  auto lock = disposable.get_lock();

  while (true) {
//     printf("[C] R\n");
//     const bool success = disposable.try_read_into(d);
//
//     if (!success) {
//         printf("[C] P\n");
//         ++pause_count;
//         continue;
//     }

    if (lock.try_lock()) {
      d = *lock;
      lock.unlock();
    } else {
      printf("[C] P\n");
      ++pause_count;
      continue;
    }

    idx = d.v[0];

    printf("[C] Idx = %llu\n", idx);

    if (std::numeric_limits<unsigned long long>::max() == idx2) {
        idx2 = idx;
    } else if (idx2 == idx) {
        printf("[C] Fail (dup) @ Idx = %llu\n", idx);
        cont.store(false);
        break;
    }

    for (size_t i = 0; i < SIZE; ++i) {
      if (d.v[i] != idx) {
        printf("[C] Fail (invalid) @ Idx = %llu\n", idx);
        cont.store(false);
        break;
      }
    }

    //std::this_thread::yield();
  }
}

int main(int argc, char **argv) {
  std::thread thr1, thr2;

  std::atomic<bool> cont{true};

  {
    Disposable<int> d;
    int v;

    bool success = d.try_read_into(v);
    assert(!success);

    v = 10;
    success = d.try_put(v);
    assert(success);

    success = d.try_read_into(v);
    assert(success);
    assert(10 == v);

    int v2 = 1;
    success = d.try_read_into(v2);
    assert(!success);
    assert(1 == v2);

    v = 11;
    success = d.try_put(v);
    assert(success);

    success = d.try_put(v);
    assert(success);

    success = d.try_read_into(v2);
    assert(success);
    assert(11 == v2);
  }

  thr1 = std::thread([&cont] () { Producer(cont); });
  thr2 = std::thread([&cont] () { Consumer(cont); });

  thr1.join();
  thr2.join();

  std::cout << "Hello, world!" << std::endl;
  return 0;
}
