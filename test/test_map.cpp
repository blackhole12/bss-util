// Copyright �2017 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#include "bss-util/Map.h"
#include <algorithm>
#include "test.h"

using namespace bss;

TESTDEF::RETPAIR test_MAP()
{
  BEGINTEST;
  Map<int, uint32_t> test;
  test.Clear();
  int ins[] = { 0,5,6,237,289,12,3 };
  int get[] = { 0,3,5,6,12 };
  uint32_t res[] = { 0,6,1,2,5 };
  uint32_t count = 0;
  TESTARRAY(ins, return test.Insert(ins[i], count++) != -1;);
  std::sort(std::begin(ins), std::end(ins));
  for(uint32_t i = 0; i < test.Length(); ++i)
  {
    TEST(test.KeyIndex(i) == ins[i]);
  }
  for(int i = 0; i < sizeof(get) / sizeof(int); ++i)
  {
    TEST(test[test.Get(get[i])] == res[i]);
  }

  TEST(test.Remove(0) == 0);
  TEST(test.Get(0) == -1);
  TEST(test.Length() == ((sizeof(ins) / sizeof(int)) - 1));

#ifndef BSS_COMPILER_GCC // Once again, GCC demonstrates its amazing ability to NOT DEFINE ANY FUCKING CONSTRUCTORS
  Map<int, FWDTEST> tst;
  tst.Insert(0, FWDTEST());
  FWDTEST lval;
  tst.Insert(1, lval);
#endif
  ENDTEST;
}
