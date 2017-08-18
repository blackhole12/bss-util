// Copyright �2017 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#include "bss-util/ThreadPool.h"
#include <algorithm>
#include "test.h"

using namespace bss;

std::atomic<size_t> pq_c;
uint16_t pq_end[TESTNUM];
std::atomic<uint16_t> pq_pos;
std::atomic<uint16_t> initcount;

void pooltest(int i) {
  initcount.fetch_add(1, std::memory_order_relaxed);
  while(!startflag.load(std::memory_order_relaxed));
  pq_end[pq_c.fetch_add(1, std::memory_order_relaxed)] = i;
}
TESTDEF::RETPAIR test_THREADPOOL()
{
  BEGINTEST;
  {
    static const int NUM = 8;
    ThreadPool pool(NUM);
    bssFill(pq_end, 0);
    pq_c = 0;
    initcount = 0;
    startflag.store(false, std::memory_order_release);

    for(int i = 0; i < TESTNUM; ++i)
      pool.AddFunc(pooltest, i);

    while(initcount.load(std::memory_order_relaxed) < NUM); // Do not allow us to continue until we've proven that all worker threads are primed, as intended
    startflag.store(true, std::memory_order_release);
    pool.Wait();

    TEST(pq_c == TESTNUM);

    std::sort(std::begin(pq_end), std::end(pq_end));
    bool check = true;
    for(size_t i = 0; i < TESTNUM; ++i)
      check = (pq_end[i] == i) && check;
    TEST(check);
  }

  ENDTEST;
}
