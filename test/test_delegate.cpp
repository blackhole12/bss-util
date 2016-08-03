// Copyright �2016 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#include "test.h"
#include "delegate.h"

using namespace bss_util;

struct foobar
{
  void BSS_FASTCALL nyan(uint32_t cat) { TEST(cat == 5); }
  void BSS_FASTCALL nyannyan(int cat, int kitty) { TEST(cat == 2); TEST(kitty == -3); }
  void BSS_FASTCALL nyannyannyan(int cat, int kitty, bool fluffy) { TEST(cat == -6); TEST(kitty == 0); TEST(fluffy); }
  void BSS_FASTCALL zoidberg() { TEST(true); ++count; }
  void nothing() {}
  void nothing2() {}
  void nothing3() {}

  TESTDEF::RETPAIR& __testret;
  int count;
};

void BSS_FASTCALL external_zoidberg(foobar* obj) { obj->zoidberg(); }

TESTDEF::RETPAIR test_DELEGATE()
{
  BEGINTEST;
  foobar foo = { __testret, 0 };
  auto first = delegate<void>::From<foobar, &foobar::zoidberg>(&foo);
  auto second = delegate<void, uint32_t>::From<foobar, &foobar::nyan>(&foo);
  auto three = delegate<void, int, int>::From<foobar, &foobar::nyannyan>(&foo);
  auto four = delegate<void, int, int, bool>::From<foobar, &foobar::nyannyannyan>(&foo);
  auto five = delegate<void>::FromC<foobar, &external_zoidberg>(&foo);

  delegate<void> copy(first);
  copy = first;
  CPU_Barrier();
  copy();
  CPU_Barrier();
  delegate<void, uint32_t> copy2(second);
  CPU_Barrier();
  copy2(5);
  CPU_Barrier();
  three(2, -3);
  four(-6, 0, true);

  bool fcalled = false;
  std::function<void()> f = [&]() { fcalled = true; };
  delegate<void> d = f;
  //delegate<void> d(std::function<void()>([](){})); // This should throw a compiler error
  d();
  TEST(fcalled);
  five();
  TEST(foo.count == 2);
  ENDTEST;
}
