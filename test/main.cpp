﻿// Copyright ©2013 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#include "Shiny.h"
#include "bss_util.h"
#include "bss_log.h"
#include "bss_algo.h"
#include "bss_alloc_additive.h"
#include "bss_alloc_fixed.h"
#include "bss_alloc_fixed_MT.h"
#include "bss_dual.h"
#include "bss_fixedpt.h"
#include "bss_sse.h"
#include "cAliasTable.h"
#include "cAnimation.h"
#include "cArrayCircular.h"
#include "cAVLtree.h"
#include "cBinaryHeap.h"
#include "cBitArray.h"
#include "cBitField.h"
#include "cBitStream.h"
#include "cBSS_Queue.h"
#include "cBSS_Stack.h"
#include "cCmdLineArgs.h"
#include "cDef.h"
#include "cDisjointSet.h"
#include "cDynArray.h"
#include "cIDHash.h"
#include "cINIstorage.h"
#include "cKDTree.h"
#include "cKhash.h"
#include "cLinkedArray.h"
#include "cLinkedList.h"
#include "cLocklessQueue.h"
#include "cMap.h"
#include "cPriorityQueue.h"
#include "cRational.h"
#include "cRefCounter.h"
#include "cScheduler.h"
//#include "cSettings.h"
#include "cSingleton.h"
#include "cSmartPtr.h"
#include "cStr.h"
#include "cStrTable.h"
#include "cThread.h"
#include "cThreadPool.h"
#include "cTRBtree.h"
#include "cTrie.h"
#include "delegate.h"
#include "lockless.h"
#include "os.h"
#include "StreamSplitter.h"
#include "Shiny.h"

#include <fstream>
#include <algorithm>
#include <limits.h> // for INT_MIN, INT_MAX etc. on GCC
#include <float.h> // FLT_EPSILON, etc. on GCC
#include <iostream>
#include <sstream>
#include <functional>
#include <atomic>
#include <thread>

#ifdef BSS_PLATFORM_WIN32
//#include "bss_win32_includes.h"
#else
#include <unistd.h>
#endif

#ifdef BSS_COMPILER_MSC
#if defined(BSS_DEBUG) && defined(BSS_CPU_x86_64)
#pragma comment(lib, "../bin/bss_util64_d.lib")
#elif defined(BSS_CPU_x86_64)
#pragma comment(lib, "../bin/bss_util64.lib")
#elif defined(BSS_DEBUG)
#pragma comment(lib, "../bin/bss_util_d.lib")
#else
#pragma comment(lib, "../bin/bss_util.lib")
#endif
#endif

#pragma warning(disable:4566)
using namespace bss_util;

// --- Define global variables ---

const unsigned short TESTNUM=50000;
unsigned short testnums[TESTNUM];
cHighPrecisionTimer _prof;
bss_Log _failedtests("../bin/failedtests.txt"); //This is spawned too early for us to save it with SetWorkDirToCur();

// --- Define testing utilities ---

struct TESTDEF
{
  typedef std::pair<size_t,size_t> RETPAIR;
  const char* NAME;
  RETPAIR (*FUNC)();
};

#define BEGINTEST TESTDEF::RETPAIR __testret(0,0)
#define ENDTEST return __testret
#define FAILEDTEST(t) BSSLOG(_failedtests,1) << "Test #" << __testret.first << " Failed  < " << MAKESTRING(t) << " >" << std::endl
#define TEST(t) { atomic_xadd(&__testret.first); try { if(t) atomic_xadd(&__testret.second); else FAILEDTEST(t); } catch(...) { FAILEDTEST(t); } }
#define TESTERROR(t, e) { atomic_xadd(&__testret.first); try { (t); FAILEDTEST(t); } catch(e) { atomic_xadd(&__testret.second); } }
#define TESTERR(t) TESTERROR(t,...)
#define TESTNOERROR(t) { atomic_xadd(&__testret.first); try { (t); atomic_xadd(&__testret.second); } catch(...) { FAILEDTEST(t); } }
#define TESTARRAY(t,f) _ITERFUNC(__testret,t,[&](uint i) -> bool { f });
#define TESTALL(t,f) _ITERALL(__testret,t,[&](uint i) -> bool { f });
#define TESTCOUNT(c,t) { for(uint i = 0; i < c; ++i) TEST(t) }
#define TESTCOUNTALL(c,t) { bool __val=true; for(uint i = 0; i < c; ++i) __val=__val&&(t); TEST(__val); }
#define TESTFOUR(s,a,b,c,d) TEST(((s)[0]==(a)) && ((s)[1]==(b)) && ((s)[2]==(c)) && ((s)[3]==(d)))
#define TESTALLFOUR(s,a) TEST(((s)[0]==(a)) && ((s)[1]==(a)) && ((s)[2]==(a)) && ((s)[3]==(a)))
#define TESTRELFOUR(s,a,b,c,d) TEST(fcompare((s)[0],(a)) && fcompare((s)[1],(b)) && fcompare((s)[2],(c)) && fcompare((s)[3],(d)))

template<class T, size_t SIZE, class F>
void _ITERFUNC(TESTDEF::RETPAIR& __testret, T (&t)[SIZE], F f) { for(uint i = 0; i < SIZE; ++i) TEST(f(i)) }
template<class T, size_t SIZE, class F>
void _ITERALL(TESTDEF::RETPAIR& __testret, T (&t)[SIZE], F f) { bool __val=true; for(uint i = 0; i < SIZE; ++i) __val=__val&&(f(i)); TEST(__val); }

template<class T>
T naivebitcount(T v)
{
  T c;
  for(c = 0; v; v >>= 1)
    c += (v & 1);
  return c;
}

template<class T>
void testbitcount(TESTDEF::RETPAIR& __testret)
{ //Use fibonacci numbers to test this
  for(T i = 0; i < (((T)1)<<(sizeof(T)<<2)); i=fbnext(i)) {
    TEST(naivebitcount<T>(i)==bitcount<T>(i));
  }
}

// This defines an enormous list of pangrams for a ton of languages, used for text processing in an attempt to expose possible unicode errors.
const char* PANGRAM = "The wizard quickly jinxed the gnomes before they vapourized.";
const bsschar* PANGRAMS[] = { 
  BSS__L("The wizard quickly jinxed the gnomes before they vapourized."),
  BSS__L("صِف خَلقَ خَودِ كَمِثلِ الشَمسِ إِذ بَزَغَت — يَحظى الضَجيعُ بِها نَجلاءَ مِعطارِ"), //Arabic
  BSS__L("Zəfər, jaketini də papağını da götür, bu axşam hava çox soyuq olacaq."), //Azeri
  BSS__L("Ах чудна българска земьо, полюшквай цъфтящи жита."), //Bulgarian
  BSS__L("Jove xef, porti whisky amb quinze glaçons d'hidrogen, coi!"), //Catalan
  BSS__L("Příliš žluťoučký kůň úpěl ďábelské ódy."), //Czech
  BSS__L("Høj bly gom vandt fræk sexquiz på wc"), //Danish
  BSS__L("Filmquiz bracht knappe ex-yogi van de wijs"), //Dutch
  BSS__L("ཨ་ཡིག་དཀར་མཛེས་ལས་འཁྲུངས་ཤེས་བློའི་གཏེར༎ ཕས་རྒོལ་ཝ་སྐྱེས་ཟིལ་གནོན་གདོང་ལྔ་བཞིན༎ ཆགས་ཐོགས་ཀུན་བྲལ་མཚུངས་མེད་འཇམ་དབྱངསམཐུས༎ མཧཱ་མཁས་པའི་གཙོ་བོ་ཉིད་འགྱུར་ཅིག།"), //Dzongkha
  BSS__L("Eble ĉiu kvazaŭ-deca fuŝĥoraĵo ĝojigos homtipon."), //Esperanto
  BSS__L("Põdur Zagrebi tšellomängija-följetonist Ciqo külmetas kehvas garaažis"), //Estonian
  BSS__L("Törkylempijävongahdus"), //Finnish
  BSS__L("Falsches Üben von Xylophonmusik quält jeden größeren Zwerg"), //German
  BSS__L("Τάχιστη αλώπηξ βαφής ψημένη γη, δρασκελίζει υπέρ νωθρού κυνός"), //Greek
  BSS__L("כך התרסק נפץ על גוזל קטן, שדחף את צבי למים"), //Hebrew
  BSS__L("दीवारबंद जयपुर ऐसी दुनिया है जहां लगभग हर दुकान का नाम हिन्दी में लिखा गया है। नामकरण की ऐसी तरतीब हिन्दुस्तान में कम दिखती है। दिल्ली में कॉमनवेल्थ गेम्स के दौरान कनॉट प्लेस और पहाड़गंज की नामपट्टिकाओं को एक समान करने का अभियान चला। पत्रकार लिख"), //Hindi
  BSS__L("Kæmi ný öxi hér, ykist þjófum nú bæði víl og ádrepa."), //Icelandic
  BSS__L("いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす（ん）"), //Japanese
  BSS__L("꧋ ꦲꦤꦕꦫꦏ꧈ ꦢꦠꦱꦮꦭ꧈ ꦥꦝꦗꦪꦚ꧈ ꦩꦒꦧꦛꦔ꧉"), //Javanese
  BSS__L("    "), //Klingon
  BSS__L("키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다."), //Korean
  BSS__L("သီဟိုဠ်မှ ဉာဏ်ကြီးရှင်သည် အာယုဝဍ္ဎနဆေးညွှန်းစာကို ဇလွန်ဈေးဘေးဗာဒံပင်ထက် အဓိဋ္ဌာန်လျက် ဂဃနဏဖတ်ခဲ့သည်။"), //Myanmar
  BSS__L("بر اثر چنین تلقین و شستشوی مغزی جامعی، سطح و پایه‌ی ذهن و فهم و نظر بعضی اشخاص واژگونه و معکوس می‌شود.‏"), //Persian
  BSS__L("À noite, vovô Kowalsky vê o ímã cair no pé do pingüim queixoso e vovó põe açúcar no chá de tâmaras do jabuti feliz."), //Portuguese
  BSS__L("Эх, чужак! Общий съём цен шляп (юфть) – вдрызг!"), //Russian
  BSS__L("Fin džip, gluh jež i čvrst konjić dođoše bez moljca."), //Serbian
  BSS__L("Kŕdeľ ďatľov učí koňa žrať kôru."), //Slovak
  BSS__L("เป็นมนุษย์สุดประเสริฐเลิศคุณค่า กว่าบรรดาฝูงสัตว์เดรัจฉาน จงฝ่าฟันพัฒนาวิชาการ อย่าล้างผลาญฤๅเข่นฆ่าบีฑาใคร ไม่ถือโทษโกรธแช่งซัดฮึดฮัดด่า หัดอภัยเหมือนกีฬาอัชฌาสัย ปฏิบัติประพฤติกฎกำหนดใจ พูดจาให้จ๊ะๆ จ๋าๆ น่าฟังเอยฯ"), //Thai
  BSS__L("ژالہ باری میں ر‌ضائی کو غلط اوڑھے بیٹھی قرۃ العین اور عظمٰی کے پاس گھر کے ذخیرے سے آناً فاناً ڈش میں ثابت جو، صراحی میں چائے اور پلیٹ میں زردہ آیا۔") //Urdu
};

template<unsigned char B, __int64 SMIN, __int64 SMAX, unsigned __int64 UMIN, unsigned __int64 UMAX, typename T>
inline void TEST_BitLimit()
{
  static_assert(std::is_same<T, BitLimit<B>::SIGNED>::value, "BitLimit failure");
  static_assert(std::is_same<std::make_unsigned<T>::type, BitLimit<B>::UNSIGNED>::value, "BitLimit failure");
  static_assert(BitLimit<B>::SIGNED_MIN==SMIN, "BitLimit failure");
  static_assert(BitLimit<B>::SIGNED_MAX==SMAX, "BitLimit failure");
  static_assert(BitLimit<B>::UNSIGNED_MIN==UMIN, "BitLimit failure");
  static_assert(BitLimit<B>::UNSIGNED_MAX==UMAX, "BitLimit failure");
}

template<typename T, size_t S> inline static size_t BSS_FASTCALL _ARRSIZE(const T (&a)[S]) { return S; }

#if defined(BSS_CPU_x86) || defined(BSS_CPU_x64)
  // This is an SSE version of the fast sqrt that calculates x*invsqrt(x) as a speed hack. Sadly, it's still slower and actually LESS accurate than the classic FastSqrt with an added iteration, below, and it isn't even portable. Left here for reference, in case you don't believe me ;)
  BSS_FORCEINLINE float sseFastSqrt(float f)
  {
    float r;
    __m128 in = _mm_load_ss(&f);
    _mm_store_ss( &r, _mm_mul_ss( in, _mm_rsqrt_ss( in ) ) );
    return r;
  }
#endif

 template<typename T>
 T calceps()
 {
    T e = (T)0.5;
    while ((T)(1.0 + (e/2.0)) != 1.0) { e /= (T)2.0; }
    return e;
 }

 
int getuniformint()
{
  static const int NUM=8;
  static int cur=NUM;
  static int samples[NUM]={0};
  if(cur>=NUM)
  {
    int last=samples[NUM-1];
    samples[0] = 0;
    for(int i=1; i<NUM; ++i)
    {
      int j = rand()%(i+1);
      samples[i]=samples[j];
      samples[j]=i;
    }
    if(last==samples[0])
    {
      int j = 1+(rand()%(NUM-1));
      samples[0]=samples[j];
      samples[j]=last;
    }
    cur=0;
  }
  return samples[cur++];
}
// --- Begin actual test procedure definitions ---

TESTDEF::RETPAIR test_bss_util_c()
{
  BEGINTEST;
  //_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  //srand((int)time(0));
  //int a=0;
  //unsigned __int64 prof = _prof.OpenProfiler();
  //CPU_Barrier();
  //for(int i = 0; i < 1000000; ++i) {
  //  //a+= 5+(rand()/(RAND_MAX + 1.0))*(15-5);
  //  a+= RANDINTGEN(9,12);
  //}
  //CPU_Barrier();
  //std::cout << _prof.CloseProfiler(prof) << " :( " << a << std::endl;

  //{
  //  float aaaa = 0;
  //  unsigned __int64 prof = _prof.OpenProfiler();
  //  CPU_Barrier();
  //  for(int i = 0; i < TESTNUM; ++i)
  //    aaaa += bssfmod<float>(-1.0f, testnums[i]+PIf);
  //  CPU_Barrier();
  //  std::cout << _prof.CloseProfiler(prof) << std::endl;
  //  std::cout << aaaa;
  //}

  std::function<void()> b([](){ int i=0; i+=1; return; });
  b = std::move(std::function<void()>([](){ return; }));

  bssCPUInfo info = bssGetCPUInfo();
  TEST(info.cores>0);
  TEST(info.SSE>2); // You'd better support at least SSE2
#ifdef BSS_64BIT
  TEST(info.flags&2); // You also have to support cmpxchg16b or lockless.h will explode
#else
  TEST(info.flags&1); // You also have to support cmpxchg8b or lockless.h will explode
#endif
  TEST(!strhex("0"));
  TEST(!strhex("z"));
  TEST(strhex("a")==10);
  TEST(strhex("8")==8);
  TEST(strhex("abad1dea")==0xABAD1DEA);
  TEST(strhex("ABAD1DEA")==0xABAD1DEA);
  TEST(strhex("0xABAD1DEA")==0xABAD1DEA);
  TEST(strhex("0xabad1dea")==0xABAD1DEA);
#ifdef BSS_PLATFORM_WIN32
  TEST(!wcshex(L"0"));
  TEST(!wcshex(L"z"));
  TEST(wcshex(L"a")==10);
  TEST(wcshex(L"8")==8);
  TEST(wcshex(L"abad1dea")==0xABAD1DEA);
  TEST(wcshex(L"ABAD1DEA")==0xABAD1DEA);
  TEST(wcshex(L"0xABAD1DEA")==0xABAD1DEA);
  TEST(wcshex(L"0xabad1dea")==0xABAD1DEA);
#endif

  char buf[6];
  TEST(itoa_r(238907,0,0,10)==22);
  TEST(itoa_r(238907,buf,0,10)==22);
  TEST(itoa_r(238907,buf,-2,10)==22);
  TEST(itoa_r(238907,buf,1,10)==22);
  itoa_r(238907,buf,2,10);
  TEST(!strcmp(buf,"7"));
  itoa_r(-238907,buf,2,10);
  TEST(!strcmp(buf,"-"));
  itoa_r(238907,buf,5,10);
  TEST(!strcmp(buf,"8907"));
  itoa_r(238907,buf,6,10);
  TEST(!strcmp(buf,"38907"));
  _itoa_r(238907,buf,10);
  TEST(!strcmp(buf,"38907"));
  _itoa_r(907,buf,10);
  TEST(!strcmp(buf,"907"));
  _itoa_r(-238907,buf,10);
  TEST(!strcmp(buf,"-8907"));
  _itoa_r(-907,buf,10);
  TEST(!strcmp(buf,"-907"));
  _itoa_r(-0,buf,10);
  TEST(!strcmp(buf,"0"));
  _itoa_r(1,buf,10);
  TEST(!strcmp(buf,"1"));
  _itoa_r(-1,buf,10);
  TEST(!strcmp(buf,"-1"));

  TEST(GetWorkingSet()!=0);

  ENDTEST;
}

TESTDEF::RETPAIR test_bss_util()
{
  BEGINTEST;
  TESTNOERROR(SetWorkDirToCur());
  TEST(bssFileSize(GetProgramPath())!=0);
  //TEST(bssFileSize(cStrW(fbuf))!=0);
  TESTNOERROR(GetTimeZoneMinutes());
  

  static_assert(std::is_same<BitLimit<sizeof(unsigned char)<<3>::SIGNED, char>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(unsigned short)<<3>::SIGNED, short>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(int)<<3>::SIGNED, int>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(char)<<3>::UNSIGNED, unsigned char>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(short)<<3>::UNSIGNED, unsigned short>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(unsigned int)<<3>::UNSIGNED, unsigned int>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(long double)<<3>::SIGNED, __int64>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(double)<<3>::SIGNED, __int64>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(float)<<3>::SIGNED, int>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(long double)<<3>::UNSIGNED, unsigned __int64>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(double)<<3>::UNSIGNED, unsigned __int64>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(std::is_same<BitLimit<sizeof(float)<<3>::UNSIGNED, unsigned int>::value, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(0)==0, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(1)==1, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(2)==2, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(3)==2, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(4)==4, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(7)==4, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(8)==8, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(20)==16, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(84)==64, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(189)==128, "Test Failure Line #" MAKESTRING(__LINE__));
  static_assert(T_CHARGETMSB(255)==128, "Test Failure Line #" MAKESTRING(__LINE__));

  //Note that these tests CAN fail if trying to compile to an unsupported or buggy platform that doesn't have two's complement.
  TEST(TBitLimit<long long>::SIGNED_MIN == std::numeric_limits<long long>::min());
  TEST(TBitLimit<long>::SIGNED_MIN == std::numeric_limits<long>::min());
  TEST(TBitLimit<int>::SIGNED_MIN == std::numeric_limits<int>::min());
  TEST(TBitLimit<short>::SIGNED_MIN == std::numeric_limits<short>::min());
  TEST(TBitLimit<char>::SIGNED_MIN == std::numeric_limits<char>::min());
  TEST(TBitLimit<long long>::SIGNED_MAX == std::numeric_limits<long long>::max());
  TEST(TBitLimit<long>::SIGNED_MAX == std::numeric_limits<long>::max());
  TEST(TBitLimit<int>::SIGNED_MAX == std::numeric_limits<int>::max());
  TEST(TBitLimit<short>::SIGNED_MAX == std::numeric_limits<short>::max());
  TEST(TBitLimit<char>::SIGNED_MAX == std::numeric_limits<char>::max());
  TEST(TBitLimit<long long>::UNSIGNED_MAX == std::numeric_limits<unsigned long long>::max());
  TEST(TBitLimit<long>::UNSIGNED_MAX == std::numeric_limits<unsigned long>::max());
  TEST(TBitLimit<int>::UNSIGNED_MAX == std::numeric_limits<unsigned int>::max());
  TEST(TBitLimit<short>::UNSIGNED_MAX == std::numeric_limits<unsigned short>::max());
  TEST(TBitLimit<char>::UNSIGNED_MAX == std::numeric_limits<unsigned char>::max());
  TEST(TBitLimit<long long>::UNSIGNED_MIN == std::numeric_limits<unsigned long long>::min());
  TEST(TBitLimit<long>::UNSIGNED_MIN == std::numeric_limits<unsigned long>::min());
  TEST(TBitLimit<int>::UNSIGNED_MIN == std::numeric_limits<unsigned int>::min());
  TEST(TBitLimit<short>::UNSIGNED_MIN == std::numeric_limits<unsigned short>::min());
  TEST(TBitLimit<char>::UNSIGNED_MIN == std::numeric_limits<unsigned char>::min());

  // These tests assume twos complement. This is ok because the previous tests would have caught errors relating to that anyway.
  TEST_BitLimit<1, -1, 0, 0, 1, char>();
  TEST_BitLimit<2, -2, 1, 0, 3, char>();
  TEST_BitLimit<7, -64, 63, 0, 127, char>();
  TEST_BitLimit<sizeof(char)<<3, CHAR_MIN, CHAR_MAX, 0, UCHAR_MAX, char>();
  TEST_BitLimit<9, -256, 255, 0, 511, short>();
  TEST_BitLimit<15, -16384, 16383, 0, 32767, short>();
  TEST_BitLimit<sizeof(short)<<3, SHRT_MIN, SHRT_MAX, 0, USHRT_MAX, short>();
  TEST_BitLimit<17, -65536, 65535, 0, 131071, int>();
  TEST_BitLimit<63, -4611686018427387904, 4611686018427387903, 0, 9223372036854775807, __int64>();
  TEST_BitLimit<64, -9223372036854775808LL, 9223372036854775807, 0, 18446744073709551615, __int64>();
  // For reference, the above strange bit values are used in fixed-point arithmetic found in bss_fixedpt.h
  
  TEST(GetBitMask<unsigned char>(4)==0x10); // 0001 0000
  TEST(GetBitMask<unsigned char>(2,4)==0x1C); // 0001 1100
  TEST(GetBitMask<unsigned char>(-2,2)==0xC7); // 1100 0111
  TEST(GetBitMask<unsigned char>(-2,-2)==0x40); // 0100 0000
  TEST(GetBitMask<unsigned char>(0,0)==0x01); // 0000 0001
  TEST(GetBitMask<unsigned char>(0,5)==0x3F); // 0011 1111
  TEST(GetBitMask<unsigned char>(0,7)==0xFF); // 1111 1111
  TEST(GetBitMask<unsigned char>(-7,0)==0xFF); // 1111 1111
  TEST(GetBitMask<unsigned char>(-5,0)==0xF9); // 1111 1001
  TEST(GetBitMask<unsigned char>(-5,-1)==0xF8); // 1111 1000
  TEST(GetBitMask<unsigned char>(-6,-3)==0x3C); // 0011 1100
  TEST(GetBitMask<unsigned int>(0,0)==0x00000001);
  TEST(GetBitMask<unsigned int>(0,16)==0x0001FFFF);
  TEST(GetBitMask<unsigned int>(12,30)==0x7FFFF000);
  TEST(GetBitMask<unsigned int>(-10,0)==0xFFC00001); 
  TEST(GetBitMask<unsigned int>(-30,0)==0xFFFFFFFD); 
  TEST(GetBitMask<unsigned int>(-12,-1)==0xFFF00000);
  TEST(GetBitMask<unsigned int>(-15,-12)==0x001e0000);
  for(uint i = 0; i < 8; ++i)
    TEST(GetBitMask<unsigned char>(i)==(1<<i));
  for(uint i = 0; i < 32; ++i)
    TEST(GetBitMask<unsigned int>(i)==(1<<i));
  for(uint i = 0; i < 64; ++i)
    TEST(GetBitMask<unsigned long long>(i)==(((unsigned __int64)1)<<i));

  int bits=9;
  bits=bssSetBit(bits,4,true);
  TEST(bits==13);
  bits=bssSetBit(bits,8,false);
  TEST(bits==5);

  std::string cpan(PANGRAM);

  strreplace(const_cast<char*>(cpan.c_str()),'m','?');
  TEST(!strchr(cpan.c_str(),'m') && strchr(cpan.c_str(),'?')!=0);
  std::basic_string<bsschar,std::char_traits<bsschar>,std::allocator<bsschar>> pan;
  for(uint i = 0; i < _ARRSIZE(PANGRAMS); ++i)
  {
    pan=PANGRAMS[i];
    bsschar f=pan[((i+7)<<3)%pan.length()];
    bsschar r=pan[((((i*13)>>3)+13)<<3)%pan.length()];
    if(f==r) r=pan[pan.length()-1];
    strreplace<bsschar>(const_cast<bsschar*>(pan.c_str()),f,r);
#ifdef BSS_PLATFORM_WIN32
    TEST(!wcschr(pan.c_str(),f) && wcschr(pan.c_str(),r)!=0);
#else
    TEST(!strchr(pan.c_str(),f) && strchr(pan.c_str(),r)!=0);
#endif
  }
  TEST(strccount<char>("10010010101110001",'1')==8);
  TEST(strccount<char>("0100100101011100010",'1')==8);
  // Linux really hates wchar_t, so getting it to assign the right character is exceedingly difficult. We manually assign 1585 instead.
  TEST(strccount<wchar_t>(L"الرِضَءَجيعُ بِهءَرِا نَجلاءَرِ رِمِعطارِ",(wchar_t)1585)==5); //L'رِ'

  int ia=0;
  int ib=1;
  std::pair<int,int> sa(1,2);
  std::pair<int,int> sb(2,1);
  std::unique_ptr<int[]> ua(new int[2]);
  std::unique_ptr<int[]> ub((int*)0);
  std::string ta("first");
  std::string tb("second");
  rswap(ia,ib);
  TEST(ia==1);
  TEST(ib==0);
  rswap(sa,sb);
  TEST((sa==std::pair<int,int>(2,1)));
  TEST((sb==std::pair<int,int>(1,2)));
  rswap(ua,ub);
  TEST(ua.get()==0);
  TEST(ub.get()!=0);
  rswap(ta,tb);
  TEST(ta=="second");
  TEST(tb=="first");

  TEST((intdiv<int,int>(10,5)==2));
  TEST((intdiv<int,int>(9,5)==1));
  TEST((intdiv<int,int>(5,5)==1));
  TEST((intdiv<int,int>(4,5)==0));
  TEST((intdiv<int,int>(0,5)==0));
  TEST((intdiv<int,int>(-1,5)==-1));
  TEST((intdiv<int,int>(-5,5)==-1));
  TEST((intdiv<int,int>(-6,5)==-2));
  TEST((intdiv<int,int>(-10,5)==-2));
  TEST((intdiv<int,int>(-11,5)==-3));

  TEST((bssmod(-1, 7)==6));
  TEST((bssmod(-90, 7)==1));
  TEST((bssmod(6,7)==6)); 
  TEST((bssmod(7,7)==0)); 
  TEST((bssmod(0,7)==0)); 
  TEST((bssmod(1, 7)==1));
  TEST((bssmod(8, 7)==1));
  TEST((bssmod(71, 7)==1));
  TEST((bssmod(1,1)==0)); 
  TEST((bssmod(-1,2)==1)); 

  TEST(fcompare(bssfmod(-1.0f, PI_DOUBLEf), 5.2831853f, 10));
  TEST(fcompare(bssfmod(-4.71f, PI_DOUBLEf), 1.57319f, 100));
  TEST(fcompare(bssfmod(1.0f, PI_DOUBLEf), 1.0f));
  TEST((bssfmod(0.0f, PI_DOUBLEf))==0.0f);
  TEST(fcompare(bssfmod(12.0f, PI_DOUBLEf), 5.716814f, 10));
  TEST(fcompare(bssfmod(90.0f, PI_DOUBLEf), 2.0354057f, 100));
  TEST(fcompare(bssfmod(-90.0f, PI_DOUBLEf), 4.2477796f, 10));

  int r[] = { -1,0,2,3,4,5,6 };
  int rr[] = { 6,5,4,3,2,0,-1 };
  bssreverse(r);
  TESTARRAY(r,return (r[0]==rr[0]);)

  const char* LTRIM = "    trim ";
  TEST(!strcmp(strltrim(LTRIM),"trim "));
  char RTRIM[] = {' ','t','r','i','m',' ',' ',0 }; // :|
  TEST(!strcmp(strrtrim(RTRIM)," trim"));
  RTRIM[5]=' ';
  TEST(!strcmp(strtrim(RTRIM),"trim"));

  unsigned int nsrc[] = { 0,1,2,3,4,5,10,13,21,2873,3829847,2654435766 };
  unsigned int num[] = { 1,2,4,5,7,8,17,21,34,4647,6193581,4292720341 };
  transform(nsrc,&fbnext<unsigned int>);
  TESTARRAY(nsrc,return nsrc[i]==num[i];)
    
  int value=8;
  int exact=value;
  int exactbefore=value;

  while(value < 100000)
  {    
    exact+=exactbefore;
    exactbefore=exact-exactbefore;
    value=fbnext(value);
  }

  TEST(tsign(2.8)==1)
  TEST(tsign(-2.8)==-1)
  TEST(tsign(23897523987453.8f)==1)
  TEST(tsign((__int64)0)==1)
  TEST(tsign(0.0)==1)
  TEST(tsign(0.0f)==1)
  TEST(tsign(-28738597)==-1)
  TEST(tsign(INT_MIN)==-1)
  TEST(tsign(INT_MAX)==1)
  TEST(tsignzero(2.8)==1)
  TEST(tsignzero(-2.8)==-1)
  TEST(tsignzero(23897523987453.8f)==1)
  TEST(tsignzero((__int64)0)==0)
  TEST(tsignzero(0.0)==0)
  TEST(tsignzero(0.0f)==0)
  TEST(tsignzero(-28738597)==-1)
  TEST(tsignzero(INT_MIN)==-1)
  TEST(tsignzero(INT_MAX)==1)

  TEST(fcompare(angledist(PI,PI_HALF),PI_HALF))
  TEST(fsmall(angledist(PI,PI+PI_DOUBLE)))
  TEST(fcompare(angledist(PI_DOUBLE+PI,PI+PI_HALF*7.0),PI_HALF))
  TEST(fcompare(angledist(PI+PI_HALF*7.0,PI_DOUBLE+PI),PI_HALF))
  TEST(fcompare(angledist(PIf,PI_HALFf),PI_HALFf))
  TEST(fsmall(angledist(PIf,PIf+PI_DOUBLEf),FLT_EPS*4))
  TEST(fcompare(angledist(PI_DOUBLEf+PIf,PIf+PI_HALFf*7.0f),PI_HALFf,9))
  TEST(fcompare(angledist(PIf+PI_HALFf*7.0f, PI_DOUBLEf+PIf), PI_HALFf, 9))
  TEST(fcompare(angledist(PIf+PI_HALFf*7.0f, PI_DOUBLEf+PIf), PI_HALFf, 9))
  TEST(fcompare(angledist(1.28f, -4.71f), 0.2931f, 10000))
  TEST(fcompare(angledist(1.28f, -4.71f+PI_DOUBLEf), 0.2931f,10000))

  TEST(fcompare(angledistsgn(PI,PI_HALF),-PI_HALF))
  TEST(fsmall(angledistsgn(PI,PI+PI_DOUBLE)))
  TEST(fcompare(angledistsgn(PI_DOUBLE+PI,PI+PI_HALF*7.0),-PI_HALF))
  TEST(fcompare(angledistsgn(PI_HALF,PI),PI_HALF))
  TEST(fcompare(angledistsgn(PIf,PI_HALFf),-PI_HALFf))
  TEST(fsmall(angledistsgn(PIf,PIf+PI_DOUBLEf),-FLT_EPS*4))
  TEST(fcompare(angledistsgn(PI_DOUBLEf+PIf,PIf+PI_HALFf*7.0f),-PI_HALFf,9))
  TEST(fcompare(angledistsgn(PI_HALFf,PIf),PI_HALFf))
  TEST(fcompare(angledistsgn(1.28f, -4.71f), 0.2931f, 10000))
  TEST(fcompare(angledistsgn(1.28f, -4.71f+PI_DOUBLEf), 0.2931f, 10000))

  const float flt=FLT_EPSILON;
  __int32 fi = *(__int32*)(&flt);
  TEST(fsmall(*(float*)(&(--fi))))
  TEST(fsmall(*(float*)(&(++fi))))
  TEST(!fsmall(*(float*)(&(++fi))))
  const double dbl=DBL_EPSILON;
  __int64 di = *(__int64*)(&dbl);
  TEST(fsmall(*(double*)(&(--di))))
  TEST(fsmall(*(double*)(&(++di))))
  TEST(!fsmall(*(double*)(&(++di))))

  TEST(fcompare(1.0f, 1.0f))
  TEST(fcompare(1.0f, 1.0f+FLT_EPSILON))
  TEST(fcompare(10.0f, 10.0f+FLT_EPSILON*10))
  TEST(fcompare(10.0f, 10.0f))
  TEST(!fcompare(0.1f, 0.1f+FLT_EPSILON*0.1f))
  TEST(!fcompare(0.1f, FLT_EPSILON))
  TEST(fcompare(1.0, 1.0))
  TEST(fcompare(1.0, 1.0+DBL_EPSILON))
  TEST(fcompare(10.0, 10.0+DBL_EPSILON*10))
  TEST(fcompare(10.0, 10.0))
  TEST(!fcompare(0.1, 0.1+DBL_EPSILON*0.1))
  TEST(!fcompare(0.1, DBL_EPSILON))

  // This tests our average aggregation formula, which lets you average extremely large numbers while maintaining a fair amount of precision.
  unsigned __int64 total=0;
  uint nc;
  double avg=0;
  double diff=0.0;
  for(nc = 1; nc < 10000;++nc)
  {
    total += nc*nc;
    avg=bssavg<double>(avg,(double)(nc*nc),nc);
    diff=bssmax(diff,fabs((total/(double)nc)-avg));
  }
  TEST(diff<FLT_EPSILON*2);

  // FastSqrt testing ground
  //
  //float a=2;
  //float b;
  //double sqrt_avg=0;
  //float NUMBERS[100000];
  ////srand(984753948);
  //for(uint i = 0; i < 100000; ++i)
  //  NUMBERS[i]=RANDFLOATGEN(2,4);

  //unsigned __int64 p=_prof.OpenProfiler();
  //CPU_Barrier();
  //for(uint j = 0; j < 10; ++j)
  //{
  //for(uint i = 0; i < 100000; ++i)
  //{
  //  a=NUMBERS[i];
  //  b=std::sqrtf(a);
  //}
  //for(uint i = 0; i < 100000; ++i)
  //{
  //  a=NUMBERS[i];
  //  b=FastSqrtsse(a);
  //}
  //for(uint i = 0; i < 100000; ++i)
  //{
  //  a=NUMBERS[i];
  //  b=FastSqrt(a);
  //}
  //}
  //CPU_Barrier();
  //sqrt_avg=_prof.CloseProfiler(p);
  //
  //TEST(b==a); //keep things from optimizing out
  //cout << sqrt_avg << std::endl;
  //CPU_Barrier();
  double ddbl = fabs(FastSqrt(2.0) - sqrt(2.0));
#ifdef BSS_COMPILER_GCC
  TEST(fabs(FastSqrt(2.0f) - sqrt(2.0f))<=FLT_EPSILON*3);
#else
  TEST(fabs(FastSqrt(2.0f) - sqrt(2.0f))<=FLT_EPSILON*2);
#endif
  TEST(fabs(FastSqrt(2.0) - sqrt(2.0))<=(DBL_EPSILON*100)); // Take note of the 100 epsilon error here on the fastsqrt for doubles.
  uint nmatch;
  for(nmatch = 1; nmatch < 200000; ++nmatch)
    if(FastSqrt(nmatch)!=(uint)sqrtl(nmatch))
      break;
  TEST(nmatch==200000);
  
  //static const int NUM=100000;
  //float _numrand[NUM];
  //for(uint i = 0; i < NUM; ++i)
  //  _numrand[i]=RANDFLOATGEN(0,100.0f);

  //int add=0;
  //unsigned __int64 prof = _prof.OpenProfiler();
  //CPU_Barrier();
  //for(uint i = 0; i < NUM; ++i)
  //  //add+=(int)_numrand[i];
  //  add+=fFastTruncate(_numrand[i]);
  //CPU_Barrier();
  //auto res = _prof.CloseProfiler(prof);
  //double avg = res/(double)NUM;
  //TEST(add>-1);
  //std::cout << "\n" << avg << std::endl;

  TEST(fFastRound(5.0f)==5);
  TEST(fFastRound(5.000001f)==5);
  TEST(fFastRound(4.999999f)==5);
  TEST(fFastRound(4.500001f)==5);
  TEST(fFastRound(4.5f)==4);
  TEST(fFastRound(5.5f)==6);
  TEST(fFastRound(5.9f)==6);
  TEST(fFastRound(5.0)==5);
  TEST(fFastRound(5.000000000)==5);
  TEST(fFastRound(4.999999999)==5);
  TEST(fFastRound(4.500001)==5);
  TEST(fFastRound(4.5)==4);
  TEST(fFastRound(5.5)==6);
  TEST(fFastRound(5.9)==6);
  TEST(fFastTruncate(5.0f)==5);
  TEST(fFastTruncate(5.000001f)==5);
  TEST(fFastTruncate(4.999999f)==4);
  TEST(fFastTruncate(4.5f)==4);
  TEST(fFastTruncate(5.5f)==5);
  TEST(fFastTruncate(5.9f)==5);
  TEST(fFastTruncate(5.0)==5);
  TEST(fFastTruncate(5.000000000)==5);
  TEST(fFastTruncate(4.999999999)==4);
  TEST(fFastTruncate(4.5)==4);
  TEST(fFastTruncate(5.5)==5);
  TEST(fFastTruncate(5.9)==5);

  //TEST(fFastDoubleRound(5.0)==(int)5.0);
  //TEST(fFastDoubleRound(5.0000000001f)==(int)5.0000000001f);
  //TEST(fFastDoubleRound(4.999999999f)==(int)4.999999999f);
  //TEST(fFastDoubleRound(4.5f)==(int)4.5f);
  //TEST(fFastDoubleRound(5.9f)==(int)5.9f); //This test fails, so don't use fFastDoubleRound for precision-critical anything.

  TEST(fcompare(distsqr(2.0f,2.0f,5.0f,6.0f),25.0f));
  TEST(fcompare(dist(2.0f,2.0f,5.0f,6.0f),5.0f,40));
  TEST(fcompare(distsqr(2.0,2.0,5.0,6.0),25.0));
  TEST(fcompare(dist(2.0,2.0,5.0,6.0),5.0,(__int64)150000)); // Do not use this for precision-critical anything.
  TEST(distsqr(2,2,5,6)==5*5); 
  TEST(dist(2,2,5,6)==5); // Yes, you can actually do distance calculations using integers, since we use FastSqrt's integer extension.

  __int64 stuff=2987452983472384720;
  unsigned short find=43271;
  TEST(bytesearch(&stuff,8,&find,1)==(((char*)&stuff)+3));
  TEST(bytesearch(&stuff,8,&find,2)==(((char*)&stuff)+3));
  TEST(bytesearch(&stuff,5,&find,1)==(((char*)&stuff)+3));
  TEST(bytesearch(&stuff,5,&find,2)==(((char*)&stuff)+3));
  TEST(bytesearch(&stuff,4,&find,1)==(((char*)&stuff)+3));
  TEST(!bytesearch(&stuff,4,&find,2));
  TEST(!bytesearch(&stuff,3,&find,2));
  TEST(!bytesearch(&stuff,0,&find,1));
  TEST(!bytesearch(&stuff,2,&find,3));
  find=27344;
  TEST(bytesearch(&stuff,2,&find,2));
  find=41;
  TEST(bytesearch(&stuff,8,&find,1)==(((char*)&stuff)+7));

  testbitcount<unsigned char>(__testret);
  testbitcount<unsigned short>(__testret);
  testbitcount<unsigned int>(__testret);
  testbitcount<unsigned __int64>(__testret);

  auto flog = [](int i) -> int { int r = 0; while(i >>= 1) ++r; return r; };
  for(nmatch = 1; nmatch < 200000; ++nmatch)
  {
    if(log2(nmatch)!=flog(nmatch))
      break;
  }
  TEST(nmatch==200000);
  for(nmatch = 2; nmatch < INT_MAX; nmatch <<= 1) // You have to do INT_MAX here even though its unsigned, because 10000... is actually less than 1111... and you overflow.
  {
    if(log2_p2(nmatch)!=flog(nmatch))
      break;
  }
  TEST(nmatch==(1<<31));
  
  TEST(fcompare(lerp<double>(3,4,0.5),3.5))
  TEST(fcompare(lerp<double>(3,4,0),3.0))
  TEST(fcompare(lerp<double>(3,4,1),4.0))
  TEST(fsmall(lerp<double>(-3,3,0.5)))
  TEST(fcompare(lerp<double>(-3,-4,0.5),-3.5))
  TEST(fcompare(lerp<float>(3,4,0.5f),3.5f))
  TEST(fcompare(lerp<float>(3,4,0),3.0f))
  TEST(fcompare(lerp<float>(3,4,1),4.0f))
  TEST(fsmall(lerp<float>(-3,3,0.5f)))
  TEST(fcompare(lerp<float>(-3,-4,0.5f),-3.5f))

  TEST((intdiv<int,int>(-5,2)==-3));
  TEST((intdiv<int,int>(5,2)==2));
  TEST((intdiv<int,int>(0,2)==0));
  TEST((intdiv<int,int>(-218937542,378)==-579200));
  TEST((intdiv<int,int>(INT_MIN,3)==((INT_MIN/3)-1)));
  TEST((intdiv<int,int>(INT_MAX,3)==(INT_MAX/3)));

  int a = -1;
  flipendian(&a);
  TEST(a==-1);
  a = 1920199968;
  flipendian(&a);
  TEST(a==552432498);

  ENDTEST;
}

TESTDEF::RETPAIR test_bss_LOG()
{
  BEGINTEST;
  std::stringstream ss;
  std::fstream fs;
  std::wstringstream wss;
  fs.open(BSS__L("黑色球体工作室.log"));
  auto tf = [&](bss_Log& di) {
    ss.clear();
    fs.clear();
    wss.clear();
    //di.AddTarget(wss);

    di.GetStream() << BSS__L("黑色球体工作室");
    di.GetStream() << "Black Sphere Studios";
    di.ClearTargets();
    di.GetStream() << BSS__L("黑色球体工作室");
  };
  bss_Log a(BSS__L("黑色球体工作室.txt"), &ss); //Supposedly 黑色球体工作室 is Black Sphere Studios in Chinese, but the literal translation appears to be Black Ball Studio. Oh well.
  bss_Log b("logtest.txt");
  b.AddTarget(fs);
  bss_Log c;
  tf(a);
  tf(b);
  tf(c);
  bss_Log d(std::move(a));

  bss_Log lg("logtest2.txt");
  lg.FORMATLOG<0>("main.cpp",-1) << std::endl;
  lg.FORMATLOG<0>("main.cpp",0) << std::endl;
  lg.FORMATLOG<0>(__FILE__,0) << std::endl;
  lg.FORMATLOG<0>(__FILE__,__LINE__) << std::endl;
  lg.FORMATLOG<0>("main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("\\main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("/main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("a\\main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("a/main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("asfsdbs dsfs ds/main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("asfsdbs dsfs ds\\main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("asfsdbs\\dsfs/ds/main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("\\asfsdbs\\dsfs/ds/main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("\\asfsdbs/dsfs\\ds/main.cpp",__LINE__) << std::endl;
  lg.FORMATLOG<0>("\\asfsdbs/dsfs\\ds/main.cpp\\",__LINE__) << std::endl;
  lg.FORMATLOG<0>("\\asfsdbs/dsfs\\ds/main.cpp/",__LINE__) << std::endl;

  ENDTEST;
}

TESTDEF::RETPAIR test_bss_algo()
{
  BEGINTEST;
  int a[] = { -5,-1,0,1,1,1,1,6,8,8,9,26,26,26,35 };
  for(int i = -10; i < 40; ++i) 
  {
    TEST((binsearch_before<int,uint,CompT<int>>(a,i)==(uint)((std::upper_bound(std::begin(a),std::end(a),i)-a)-1)));
    TEST((binsearch_after<int,uint,CompT<int>>(a,i)==(std::lower_bound(std::begin(a),std::end(a),i)-a)));
  }

  int b[2] = { 2,3 };
  int d[1] = { 1 };
  TEST((binsearch_exact<int,uint,CompT<int>>(b,1)==-1));
  TEST((binsearch_exact<int,uint,CompT<int>>(b,2)==0));
  TEST((binsearch_exact<int,uint,CompT<int>>(b,3)==1));
  TEST((binsearch_exact<int,uint,CompT<int>>(b,4)==-1));
  TEST((binsearch_exact<int,int,uint,CompT<int>>(0,0,0,0)==-1));
  TEST((binsearch_exact<int,int,uint,CompT<int>>(0,-1,0,0)==-1));
  TEST((binsearch_exact<int,int,uint,CompT<int>>(0,1,0,0)==-1));
  TEST((binsearch_exact<int,uint,CompT<int>>(d,-1)==-1));
  TEST((binsearch_exact<int,uint,CompT<int>>(d,1)==0));
  TEST((binsearch_exact<int,int,uint,CompT<int>>(d,1,1,1)==-1));
  TEST((binsearch_exact<int,uint,CompT<int>>(d,2)==-1));

  srand(90);
  rand();
  shuffle(a);
  TEST(a[0]!=-5);
  TEST(a[14]!=35);

  BSS_ALIGN(16) float v[4]={2,-3,4,-5};
  BSS_ALIGN(16) float u[4]={1,-2,-1,2};
  BSS_ALIGN(16) float w[4]={0,0,0,0};
  TEST(fcompare(NVectDistSq(v,w),54.0f));
  TEST(fcompare(NVectDist(v,w),7.34846922835f));
  TEST(fcompare(NVectDist(u,v),NVectDist(v,u)));
  TEST(fcompare(NVectDist(u,v),8.717797887f));
  TEST(fcompare(NTriangleArea(v,u,w),11.22497216f,100));
  TEST(fcompare(NTriangleArea(u,v,w),11.22497216f,100));
  TEST(fsmall(NDot(u,w)));
  TEST(fcompare(NDot(u,v),-6.0f));
  NVectAdd(v,w,w);
  TESTFOUR(w,v[0],v[1],v[2],v[3]);
  NVectAdd(v,3.0f,w);
  TESTFOUR(w,5,0,7,-2);
  NVectAdd(v,u,w);
  TESTRELFOUR(w,3,-5,3,-3);
  NVectSub(v,3.0f,w);
  TESTRELFOUR(w,-1,-6,1,-8);
  NVectSub(v,u,w);
  TESTRELFOUR(w,1,-1,5,-7);
  NVectMul(v,2.5f,w);
  TESTRELFOUR(w,5,-7.5f,10,-12.5f);
  NVectMul(v,u,w);
  TESTRELFOUR(w,2,6,-4,-10);
  NVectDiv(v,2.5f,w);
  TESTRELFOUR(w,0.8f,-1.2f,1.6f,-2);
  NVectDiv(v,u,w);
  TESTRELFOUR(w,2,1.5f,-4,-2.5f);

  NormalZig<128> zig; // TAKE OFF EVERY ZIG!
  float rect[4] = { 0,100,1000,2000 };
  StochasticSubdivider<float>(rect,
    [](unsigned int d, const float (&r)[4]) -> double { return 1.0 - 10.0/(d+10.0) + 0.5/(((r[2]-r[0])*(r[3]-r[1]))+0.5); },
    [](const float(&r)[4]) { },
    [&](unsigned int d, const float (&r)[4]) -> double { return zig(); });

  PoissonDiskSample<float>(rect,4.0f,[](float* f)->float{ return f[0]+f[1];});

  BSS_ALIGN(16) float m1[4][4]={ {1,2,4,8}, {16,32,64,128}, {-1,-2,-4,-8 }, { -16,-32,-64,-128 } };
  BSS_ALIGN(16) float m2[4][4]={ {8,4,2,1}, {16,32,64,128}, {-1,-32,-4,-16 }, { -8,-2,-64,-128 } };
  BSS_ALIGN(16) float m3[4][4]={0};
  BSS_ALIGN(16) float m4[4][4]={0};
  BSS_ALIGN(16) float m5[4][4]={{-28, -76, -398, -831}, {-448, -1216, -6368, -13296}, {28, 76, 398, 831}, {448, 1216, 6368, 13296}};
  BSS_ALIGN(16) float m6[4][4]={{54, 108, 216, 432}, {-1584, -3168, -6336, -12672}, {-253, -506, -1012, -2024}, {2072, 4144, 8288, 16576}};
  Mult4x4(m3,m1,m2);
  Mult4x4(m4,m2,m1);
  TEST(!memcmp(m3,m5,sizeof(float)*4*4));
  TEST(!memcmp(m4,m6,sizeof(float)*4*4));
  //TEST(QuadraticBSpline<double,double>(1.0,2.0,4.0,8.0)==4.0);
  double res=CubicBSpline<double,double>(0.0,2.0,4.0,8.0,16.0);
  TEST(res==4.0);
  res=CubicBSpline<double,double>(1.0,2.0,4.0,8.0,16.0);
  TEST(res==8.0);

  ENDTEST;
}

template<class T, typename P, int MAXSIZE, int TRIALS>
void TEST_ALLOC_FUZZER_THREAD(TESTDEF::RETPAIR& __testret,T& _alloc, cDynArray<cArraySimple<std::pair<P*,size_t>>>& plist)
{
  for(int j=0; j<5; ++j)
  {
    bool pass=true;
    for(int i = 0; i < TRIALS; ++i)
    {
      if(RANDINTGEN(0,10)<5 || plist.Length()<3)
      {
        size_t sz = RANDINTGEN(1,MAXSIZE);
        P* test=(P*)_alloc.alloc(sz);
        for(int i = 0; i<sz; ++i) *(char*)(test+i) = i+1;
        plist.Add(std::pair<P*,size_t>(test,sz));
      }
      else
      {
        int index=RANDINTGEN(0,plist.Length());
        for(int i = 0; i<plist[index].second; ++i) {
          if(((char)(i+1) != *(char*)(plist[index].first+i)))
            pass = false;
        }
        memset(plist[index].first, 0, sizeof(P)*plist[index].second);
        _alloc.dealloc(plist[index].first);
        rswap(plist.Back(),plist[index]);
        plist.RemoveLast(); // This little technique lets us randomly remove items from the array without having to move large chunks of data by swapping the invalid element with the last one and then removing the last element (which is cheap)
      }
    }
    TEST(pass);
    plist.Clear(); // BOY I SHOULD PROBABLY CLEAR THIS BEFORE I PANIC ABOUT INVALID MEMORY ALLOCATIONS, HUH?
    _alloc.Clear();
  }
}

template<class T, typename P, int MAXSIZE>
void TEST_ALLOC_FUZZER(TESTDEF::RETPAIR& __testret)
{
  cDynArray<cArraySimple<std::pair<P*,size_t>>> plist;
  for(int k=0; k<10; ++k)
  {
    T _alloc;
    TEST_ALLOC_FUZZER_THREAD<T,P,MAXSIZE,10000>(__testret,_alloc,plist);
  }
}

TESTDEF::RETPAIR test_bss_ALLOC_ADDITIVE()
{
  BEGINTEST;
  TEST_ALLOC_FUZZER<cAdditiveAlloc,char,400>(__testret);
  ENDTEST;
}

TESTDEF::RETPAIR test_bss_ALLOC_FIXED()
{
  BEGINTEST;
  TEST_ALLOC_FUZZER<cFixedAlloc<size_t>,size_t,1>(__testret);
  ENDTEST;
}

#ifdef BSS_PLATFORM_WIN32
#define BSS_PFUNC_PRE unsigned int BSS_COMPILER_STDCALL
#else
#define BSS_PFUNC_PRE void*
#endif

std::atomic<bool> startflag;

template<class T, typename P>
void TEST_ALLOC_MT(TESTDEF::RETPAIR& pair, T& p)
{
  while(!startflag);
  cDynArray<cArraySimple<std::pair<P*,size_t>>> plist;
  TEST_ALLOC_FUZZER_THREAD<T, P, 1, 50000>(pair, p, plist);
}

template<class T>
struct MTALLOCWRAP : cLocklessFixedAlloc<T> { inline MTALLOCWRAP(size_t init=8) : cLocklessFixedAlloc<T>(init) {} inline void Clear() {} };

TESTDEF::RETPAIR test_bss_ALLOC_FIXED_LOCKLESS()
{
  BEGINTEST;
  MTALLOCWRAP<size_t> _alloc(10000);

  const int NUM = 16;
  std::thread threads[NUM];
  startflag=false;
  for(int i = 0; i < NUM; ++i)
    threads[i] = std::thread(TEST_ALLOC_MT<MTALLOCWRAP<size_t>, size_t>, std::ref(__testret), std::ref(_alloc));
  startflag=true;

  for(int i = 0; i < NUM; ++i)
    threads[i].join();

  MTALLOCWRAP<size_t> _alloc2;

  for(int i = 0; i < NUM; ++i)
    threads[i] = std::thread(TEST_ALLOC_MT<MTALLOCWRAP<size_t>, size_t>, std::ref(__testret), std::ref(_alloc2));
  
  for(int i = 0; i < NUM; ++i)
    threads[i].join();

  //std::cout << _alloc2.contention << std::endl;
  //std::cout << _alloc2.grow_contention << std::endl;
  ENDTEST;
}

TESTDEF::RETPAIR test_bss_deprecated()
{
  std::vector<bool> test;
  BEGINTEST;
  time_t tmval=TIME64(NULL);
  TEST(tmval!=0);
  TIME64(&tmval);
  TEST(tmval!=0);
  tm tms;
  TEST([&]()->bool { GMTIMEFUNC(&tmval,&tms); return true; }())
  //TESTERR(GMTIMEFUNC(0,&tms));
  //char buf[12];
//#define VSPRINTF(dest,length,format,list) _vsnprintf_s(dest,length,length,format,list)
//#define VSWPRINTF(dest,length,format,list) _vsnwprintf_s(dest,length,length,format,list)
  FILE* f=0;
  FOPEN(f,"__valtest.txt","wb");
  TEST(f!=0);
  f=0;
  WFOPEN(f,BSS__L("石石石石shi.txt"),BSS__L("wb"));
  TEST(f!=0);
  size_t a = 0;
  size_t b = -1;
  MEMCPY(&a,sizeof(size_t),&b,sizeof(size_t));
  TEST(a==b);
  a=0;
  MEMCPY(&a,sizeof(size_t)-1,&b,sizeof(size_t)-1);
  TEST(a==(b>>8));

  char buf[256];
  buf[9]=0;
  STRNCPY(buf,11,PANGRAM,10);
  STRCPY(buf,256,PANGRAM);
  STRCPYx0(buf,PANGRAM);

#ifdef BSS_PLATFORM_WIN32
  wchar_t wbuf[256];
  wbuf[9]=0;
  WCSNCPY(wbuf,11,PANGRAMS[3],10);
  WCSCPY(wbuf,256,PANGRAMS[2]);
  WCSCPYx0(wbuf,PANGRAMS[4]);
  TEST(!WCSICMP(L"Kæmi ný",L"kæmi ný"));
#endif

  TEST(!STRICMP("fOObAr","Foobar"));

//#define STRTOK(str,delim,context) strtok_s(str,delim,context)
//#define WCSTOK(str,delim,context) wcstok_s(str,delim,context)
//#define SSCANF sscanf_s
  ENDTEST;
}

TESTDEF::RETPAIR test_bss_FIXEDPT()
{
  BEGINTEST;

  FixedPt<13> fp(23563.2739);
  float res=fp;
  fp+=27.9;
  res+=27.9f;
  TEST(fcompare(res,fp));
  res=fp;
  fp-=8327.9398437;
  res-=8327.9398437f;
  TEST(fcompare(res,fp));
  res=fp;
  fp*=6.847399;
  res*=6.847399f;
  TEST(fcompare(res,fp,215)); // We start approaching the edge of our fixed point range here so things predictably get out of whack
  res=fp;
  fp/=748.9272;
  res/=748.9272f;
  TEST(fcompare(res,fp,6));
  ENDTEST;
}

bool GRAPHACTION(unsigned short s) { return false; }
bool GRAPHISEDGE(const __edge_MaxFlow<__edge_LowerBound<void>>* e) { return e->capacity>0; }

template<class G> // debug
void outgraph(const G& g)
{
  std::cout << "--------" << std::endl;
  auto& n = g.GetNodes();
  for(const G::N_& node : n) // test range-based for loops
  {
    for(auto p = node.to; p!=0; p = p->next)
      std::cout << p->from << " -> (" << p->data.capacity << "," << p->data.flow << ") ->" << p->to << std::endl;
  }
}

TESTDEF::RETPAIR test_bss_GRAPH()
{
  BEGINTEST;

  {
    Graph<void,void> g(6);

    ushort s = 0;
    ushort o = 1;
    ushort p = 2;
    ushort q = 3;
    ushort r = 4;
    ushort t = 5;
    g.AddEdge(s,o,0); 
    g.AddEdge(s,p,0); 
    g.AddEdge(o,p,0); 
    g.AddEdge(o,q,0); 
    g.AddEdge(p,r,0); 
    g.AddEdge(q,r,0); 
    g.AddEdge(q,t,0);
    g.AddEdge(r,t,0); 
    TEST(g.NumEdges()==8);
    TEST(g.NumNodes()==6);

    ushort* queue = (ushort*)_alloca(sizeof(ushort)*g.NumNodes());
    BreadthFirstGraph<Graph<void,void>,GRAPHACTION>(g,s,queue);
    TEST(queue[0]==p);
    TEST(queue[1]==o);
    TEST(queue[2]==r);
    TEST(queue[3]==q);
    TEST(queue[4]==t);
  }

  {
    Graph<__edge_MaxFlow<void>,void> g;
    
    ushort s = g.AddNode();
    ushort o = g.AddNode();
    ushort p = g.AddNode();
    ushort q = g.AddNode();
    ushort r = g.AddNode();
    TEST(g.Back()==r);
    ushort t = g.AddNode();
    TEST(g.Front()==s);
    TEST(g.Back()==t);
    { __edge_MaxFlow<void> e = { 0,3 }; g.AddEdge(s,o,&e); }
    { __edge_MaxFlow<void> e = { 0,3 }; g.AddEdge(s,p,&e); }
    { __edge_MaxFlow<void> e = { 0,2 }; g.AddEdge(o,p,&e); }
    { __edge_MaxFlow<void> e = { 0,3 }; g.AddEdge(o,q,&e); }
    { __edge_MaxFlow<void> e = { 0,2 }; g.AddEdge(p,r,&e); }
    { __edge_MaxFlow<void> e = { 0,4 }; g.AddEdge(q,r,&e); }
    { __edge_MaxFlow<void> e = { 0,2 }; g.AddEdge(q,t,&e); }
    { __edge_MaxFlow<void> e = { 0,3 }; g.AddEdge(r,t,&e); }
    
    MaxFlow_PushRelabel(g,s,t);
    int res[] = { g[s].to->data.flow, g[s].to->next->data.flow, g[o].to->data.flow, g[o].to->next->data.flow, 
                  g[p].to->data.flow, g[q].to->data.flow, g[q].to->next->data.flow, g[r].to->data.flow };
    int resref[] = { 2,3,3,0,2,2,1,3 };
    for(int i = 0; i < sizeof(res)/sizeof(int); ++i) TEST(res[i]==resref[i]);
  }

  {
    __vertex_Demand<void> verts[4] = { {-3}, {-3}, {2}, {4} };
    Graph<__edge_MaxFlow<void>, __vertex_Demand<void>> g(4, verts);
    { __edge_MaxFlow<void> e = {0, 3}; g.AddEdge(0, 2, &e); } // Note, this problem has two valid answers. If the edges are added in 
    { __edge_MaxFlow<void> e = {0, 3}; g.AddEdge(0, 1, &e); } // the opposite order, you'll get a different one.
    { __edge_MaxFlow<void> e = {0, 2}; g.AddEdge(1, 3, &e); }
    { __edge_MaxFlow<void> e = {0, 2}; g.AddEdge(1, 2, &e); }
    { __edge_MaxFlow<void> e = {0, 2}; g.AddEdge(2, 3, &e); }

    ushort s;
    ushort t;
    Circulation_MaxFlow(g,s,t);
    MaxFlow_PushRelabel(g, s, t);
    //outgraph(g);
    g.RemoveNode(s);
    g.RemoveNode(t);
    int res[] = { g[0].to->data.flow, g[0].to->next->data.flow, g[1].to->data.flow, g[1].to->next->data.flow, g[2].to->data.flow };
    int resref[] = { 1,2,2,2,2 };
    for(int i = 0; i < sizeof(res)/sizeof(int); ++i) TEST(res[i]==resref[i]);
  }

  {
    typedef __edge_MaxFlow<__edge_LowerBound<void>> EDGE;
    EDGE m[16] = { {0,0,0},{0,4,1},{0,5,2},{0,0,0}, 
                 {0,0,0},{0,0,0},{0,2,0},{0,2,0},
                 {0,0,0},{0,0,0},{0,0,0},{0,2,0},
                 {0,0,0},{0,0,0},{0,0,0},{0,0,0} };
    __vertex_Demand<void> verts[4] = { {-6}, {-2}, {4}, {4} };
    Graph<__edge_MaxFlow<__edge_LowerBound<void>>,__vertex_Demand<void>> g;
    g.FromMatrix<GRAPHISEDGE>(4,m,verts);

    ushort s;
    ushort t;
    LowerBound_Circulation(g);
    Circulation_MaxFlow(g,s,t);
    MaxFlow_PushRelabel(g,s,t);
    g.RemoveNode(s);
    g.RemoveNode(t);
    int res[] = { g[0].to->data.flow, g[0].to->next->data.flow, g[1].to->data.flow, g[1].to->next->data.flow, g[2].to->data.flow };
    int resref[] = { 3,0,2,1,2 }; // This adds the edges in a different order so we get the other legal answer
    for(int i = 0; i < sizeof(res)/sizeof(int); ++i) TEST(res[i]==resref[i]);
  }

  { 
    char m[16] = { 0,1,0,0,
                 0,0,1,0,
                 1,1,0,1,
                 0,1,0,0, };
    Graph<void,void> g(4,m,0);

    char m2[16]={0};
    g.ToMatrix(m2,0);
    TEST(!memcmp(m,m2,sizeof(m)));
  }
  ENDTEST;
}

BSS_FORCEINLINE bool BSS_FASTCALL fsmallcomp(double af, double bf, __int64 maxDiff=1)
{
  if(af==0.0)
    return fsmall(bf);
  if(bf==0.0)
    return fsmall(af);
  return fcompare(af,bf,maxDiff);
}

TESTDEF::RETPAIR test_bss_DUAL()
{
  BEGINTEST;
  // d/dx( e^(x + x^2) + 2x/5 + cos(x*x) + sin(ln(x)+log(x)) ) = e^(x^2+x) (2 x+1)-2 x sin(x^2)+((1+log(10)) cos((1+1/(log(10))) log(x)))/(x log(10))+2/5
  
  for(int i = -10; i < 10; ++i)
  {
    Dual<double,2> dx(i,1); //declare our variable
    Dual<double,2> adx(abs(i),1);
    //auto ax = (exp(dx + (dx^2)) + (2*dx)/5 + cos(abs(x)^abs(x)) + sin(log(dx)+log10(dx)));
    double id=i;
    auto ax = (dx + (dx^2)).exp() + (2.0*dx)/5.0;
    TEST(fsmallcomp(ax(0),exp((i*i)+id) + (2.0*i/5.0)));
    TEST(fsmallcomp(ax(1),exp((i*i)+id)*(2*i+1) + 2.0/5.0));
    TEST(fsmallcomp(ax(2),exp((i*i)+id)*(4*i*i + 4*i + 3)));

    if(!i) continue; //the functions below can't handle zeros.
    id=abs(i);
    ax = (adx^adx).cos();
    TEST(fsmallcomp(ax(0),cos(std::pow(id,id))));
    TEST(fsmallcomp(ax(1),sin(std::pow(id,id))*(-std::pow(id,id))*(log(id)+1)));
    //TEST(ax(2)==(cos(std::pow(id,id))));
    
    ax = (adx.log() + adx.log10()).sin();
    TEST(fsmallcomp(ax(0),sin(log(id)+log10(id))));
    TEST(fsmallcomp(ax(1),((1+log(10.0))*cos((1+(1/(log(10.0))))*log(id)))/(id*log(10.0)),5));
    //TEST(ax(2)==(-((1+log(10.0))*((1+log(10.0))*sin((1+1/(log(10.0))) log(id))+log(10.0)*cos((1+(1/(log(10.0))))*log(id))))/((id*id)*(log(10.0)*log(10.0)))));
  }

  ENDTEST;
}

// This does not perform well at all
template<int M1,int M2,int M3,int M4>
inline int PriCompare(const int (&li)[4],const int (&ri)[4])
{
  sseVeci m(M1,M2,M3,M4); // positive mask
  sseVeci n(-M1,-M2,-M3,-M4); // negative mask
  //sseVeci l(l1,l2,l3,l4);
  sseVeci l(li);
  //sseVeci r(r1,r2,r3,r4);
  sseVeci r(ri);
  sseVeci t=((l>r)&m) + ((l<r)&n);
  l=BSS_SSE_SHUFFLE_EPI32(t,0x1B); // Assign t to l, but reversed
  l+=t;
  t = _mm_castps_si128(_mm_movehl_ps(_mm_castsi128_ps(t),_mm_castsi128_ps(l))); // Move upper 2 ints to bottom 2 ints
  return _mm_cvtsi128_si32(t+l); // return bottom 32bit result
}

BSS_FORCEINLINE int PriComp(int l1,int l2,int l3,int l4,int r1,int r2,int r3,int r4)
{
  BSS_ALIGN(16) int li[4]={l1,l2,l3,l4};
  BSS_ALIGN(16) int ri[4]={r1,r2,r3,r4};
  return PriCompare<1,2,4,8>(li,ri);
}

inline static unsigned int Interpolate(unsigned int l, unsigned int r, float c)
{
  //float inv=1.0f-c;
  //return ((unsigned int)(((l&0xFF000000)*inv)+((r&0xFF000000)*c))&0xFF000000)|
  //        ((unsigned int)(((l&0x00FF0000)*inv)+((r&0x00FF0000)*c))&0x00FF0000)|
	 //       ((unsigned int)(((l&0x0000FF00)*inv)+((r&0x0000FF00)*c))&0x0000FF00)|
		//	    ((unsigned int)(((l&0x000000FF)*inv)+((r&0x000000FF)*c))&0x000000FF);
  //BSS_SSE_M128i xl = _mm_set1_epi32(l); // duplicate l 4 times in the 128-bit register (l,l,l,l)
  //BSS_SSE_M128i xm = _mm_set_epi32(0xFF000000,0x00FF0000,0x0000FF00,0x000000FF); // Channel masks (alpha,red,green,blue)
  //xl=_mm_and_si128(xl,xm); // l&mask
  //xl=_mm_shufflehi_epi16(xl,0xB1); // Now we have to shuffle these values down because there is no way to convert an unsigned int to a float. In any instruction set. Ever.
  //BSS_SSE_M128 xfl = _mm_cvtepi32_ps(xl); // Convert to float
  //BSS_SSE_M128 xc = _mm_set_ps1(c); // (c,c,c,c)
  //BSS_SSE_M128 xinv = _mm_set_ps1(1.0f);  // (1.0,1.0,1.0,1.0)
  //xinv = _mm_sub_ps(xinv,xc); // (1.0-c,1.0-c,1.0-c,1.0-c)
  //xfl = _mm_mul_ps(xfl,xinv); // Multiply l by 1.0-c (inverted factor)
  //BSS_SSE_M128i xr = _mm_set1_epi32(r); // duplicate r 4 times across the 128-bit register (r,r,r,r)
  //xr=_mm_and_si128(xr,xm); // r & mask
  //xr=_mm_shufflehi_epi16(xr,0xB1); // Do the same shift we did on xl earlier so they match up
  //BSS_SSE_M128 xrl = _mm_cvtepi32_ps(xr); // convert to float
  //xrl = _mm_mul_ps(xrl,xc); // Multiply r by c
  //xfl = _mm_add_ps(xfl,xrl); // Add l and r
  //xl = _mm_cvttps_epi32(xfl); // Convert back to integer
  //xl=_mm_shufflehi_epi16(xl,0xB1); // Shuffle the last two back up (this is actually the same shuffle, since before we just swapped locations, so we swap locations again and then we're back where we started).
  //xl = _mm_and_si128(xl,xm); // l&mask
  //xr = xl;
  //xr = _mm_shuffle_epi32(xr,0x1B); // Reverses the order of xr so we now have (d,c,b,a)
  //xl = _mm_or_si128(xl,xr); // Or xl and xr so we get (d|a,c|b,b|c,a|d) in xl
  //xr = _mm_castps_si128(_mm_movehl_ps(_mm_castsi128_ps(xr),_mm_castsi128_ps(xl))); // Move upper 2 ints to bottom 2 ints in xr so xr = (d,c,d|a,c|b)
  //xl = _mm_or_si128(xl,xr); // Now or them again so we get (d|a,c|b,b|c | d|a,a|d | c|b) which lets us take out the bottom integer as our result
  
  sseVeci xl(l); // duplicate l 4 times in the 128-bit register (l,l,l,l)
  sseVeci xm(0x000000FF,0x0000FF00,0x00FF0000,0xFF000000); // Channel masks (alpha,red,green,blue), these are loaded in reverse order.
  xl=sseVeci::ShuffleHi<0xB1>(xl&xm); // Now we have to shuffle (l&m) down because there is no way to convert an unsigned int xmm register to a float. In any instruction set. Ever.
  sseVec xc(c); // (c,c,c,c)
  sseVeci xr(r); // duplicate r 4 times across the 128-bit register (r,r,r,r)
  xr=sseVeci::ShuffleHi<0xB1>(xr&xm); // Shuffle r down just like l
  xl=((sseVec(xr)*xc)+(sseVec(xl)*(sseVec(1.0f)-xc))); //do the operation (r*c) + (l*(1.0-c)) across all 4 integers, converting to and from floating point in the process.
  xl=sseVeci::ShuffleHi<0xB1>(xl); // reverse our shuffling from before (this is actually the same shuffle, since before we just swapped locations, so we swap locations again, and then we're back where we started).
  xl&=xm; // mask l with m again.
  xr = sseVeci::Shuffle<0x1B>(xl); // assign the values of xl to xr, but reversed, so we have (d,c,b,a)
  xl|=xr; // OR xl and xr so we get (d|a,c|b,b|c,a|d) in xl
  xr = _mm_castps_si128(_mm_movehl_ps(_mm_castsi128_ps(xr),_mm_castsi128_ps(xl))); // Move upper 2 ints to bottom 2 ints in xr so xr = (d,c,d|a,c|b)
  return (unsigned int)_mm_cvtsi128_si32(xl|xr); // Now OR them again so we get (d|a,c|b,b|c | d|a,a|d | c|b), then store the bottom 32-bit integer. What kind of fucked up name is _mm_cvtsi128_si32 anyway?
}

static unsigned int flttoint(const float (&ch)[4])
{
  //return (((uint)(ch[0]*255.0f))<<24)|(((uint)(ch[1]*255.0f))<<16)|(((uint)(ch[2]*255.0f))<<8)|(((uint)(ch[3]*255.0f)));
  sseVeci xch=(BSS_SSE_SHUFFLEHI_EPI16(sseVeci(sseVec(ch)*sseVec(255.0f,65280.0f,255.0f,65280.0f)),0xB1));
  xch&=sseVeci(0x000000FF,0x0000FF00,0x00FF0000,0xFF000000);
  sseVeci xh = BSS_SSE_SHUFFLE_EPI32(xch,0x1B); // assign the values of xl to xr, but reversed, so we have (d,c,b,a)
  xch|=xh; // OR xl and xr so we get (d|a,c|b,b|c,a|d) in xl
  xh = _mm_castps_si128(_mm_movehl_ps(_mm_castsi128_ps(xh),_mm_castsi128_ps(xch))); // Move upper 2 ints to bottom 2 ints in xr so xr = (d,c,d|a,c|b)
  return (unsigned int)_mm_cvtsi128_si32(xch|xh);
}

static void inttoflt(unsigned int from, float (&ch)[4])
{
  sseVec c(BSS_SSE_SHUFFLEHI_EPI16(sseVeci(from)&sseVeci(0x000000FF,0x0000FF00,0x00FF0000,0xFF000000),0xB1));
  (c/sseVec(255.0f,65280.0f,255.0f,65280.0f)) >> ch; 
}

TESTDEF::RETPAIR test_bss_SSE()
{
  BEGINTEST;

  CPU_Barrier();
  uint r=Interpolate(0xFF00FFAA,0x00FFAACC,0.5f);
  sseVeci xr(r);
  sseVeci xz(r);
  xr+=xz;
  xr-=xz;
  xr&=xz;
  xr|=xz;
  xr^=xz;
  xr>>=5;
  xr<<=3;
  xr<<=xz;
  xr>>=xz;
  sseVeci xw(r>>3);
  xw= ((xz+xw)|(xz&xw))-(xw<<2)+((xz<<xw)^((xz>>xw)>>1));
  sseVeci c1(xw==r);
  sseVeci c2(xw!=r);
  sseVeci c3(xw<r);
  sseVeci c4(xw>r);
  sseVeci c5(xw<=r);
  sseVeci c6(xw>=r);
  CPU_Barrier();
  TEST(r==0x7F7FD4BB);
  BSS_ALIGN(16) int rv[4] = { -1, -1, -1, -1 };
  sseVeci::ZeroVector()>>rv;
  TESTALLFOUR(rv,0);
  xz >> rv;
  TESTALLFOUR(rv,2139083963);
  xw >> rv;
  TESTALLFOUR(rv,1336931703);
  c1 >> rv;
  TESTALLFOUR(rv,0);
  c2 >> rv;
  TESTALLFOUR(rv,-1);
  c3 >> rv;
  TESTALLFOUR(rv,-1);
  c4 >> rv;
  TESTALLFOUR(rv,0);
  c5 >> rv;
  TESTALLFOUR(rv,-1);
  c6 >> rv;
  TESTALLFOUR(rv,0);
  CPU_Barrier();

  BSS_ALIGN(16) short rw[8] = {0,1,2,3,4,5,6,7};
  sseVeci16 w1(rw);
  sseVeci16 w2(rw);
  sseVeci16 w3(((w1+w2)&(sseVeci16(4)|sseVeci16(2)))-w2);
  sseVeci16 w4(w3<w1);

  BSS_ALIGN(16) float ch[4] = { 1.0f, 0.5f, 0.5f,1.0f };
  uint chr=flttoint(ch);
  TEST(chr==0xFF7F7FFF);
  inttoflt(chr,ch);
  TESTRELFOUR(ch,1.0f,0.49803922f,0.49803922f,1.0f);
  CPU_Barrier();

  BSS_ALIGN(16) float arr[4] = { -1,-2,-3,-5 };
  float uarr[4] = { -1,-2,-3,-4 };
  sseVec u(1,2,3,4);
  sseVec v(5);
  sseVec w(arr);
  w >> arr;
  TESTFOUR(arr,-1,-2,-3,-5)
  w = sseVec(BSS_UNALIGNED<const float>(uarr));
  w >> arr;
  TESTFOUR(arr,-1,-2,-3,-4)
  sseVec uw(u*w);
  uw >> arr;
  TESTFOUR(arr,-1,-4,-9,-16)
  sseVec uv(u*v);
  uv >> arr;
  TESTFOUR(arr,5,10,15,20)
  sseVec u_w(u/v);
  u_w >> arr;
  TESTFOUR(arr,0.2f,0.4f,0.6f,0.8f)
  sseVec u_v(u/w);
  u_v >> arr;
  TESTFOUR(arr,-1,-1,-1,-1)
  u_v = uw*w/v+u*v-v/w;
  u_v >> arr;
  TESTRELFOUR(arr,10.2f,14.1f,22.0666666f,34.05f)
  (u/w + v - u) >> arr;
  TESTFOUR(arr,3,2,1,0)
  (u/w + v - u) >> BSS_UNALIGNED<float>(uarr);
  TESTFOUR(uarr,3,2,1,0)
  sseVec m3(1,3,-1,-2);
  sseVec m4(0,4,-2,-1);
  sseVec ab = m3.max(m4);
  ab >> arr;
  TESTFOUR(arr,1,4,-1,-1)
  ab = m3.min(m4);
  ab >> arr;
  TESTFOUR(arr,0,3,-2,-2)

  //int megatest[TESTNUM*10];
  //for(uint i = 0; i<TESTNUM*10; ++i)
  //  megatest[i]=log2(i);

  //for(int k=0; k < 30; ++k)
  //{
  //char prof;

  //shuffle(megatest);
  //int l=0;
  //prof=_prof.OpenProfiler();
  //CPU_Barrier();
  //int v;
  //for(int i = 0; i < 1000000; i+=8) {
  //  v=PriComp(megatest[i+0],megatest[i+1],megatest[i+2],megatest[i+3],megatest[i+4],megatest[i+5],megatest[i+6],megatest[i+7]);
  //  l+=SGNCOMPARE(v,0);
  //}
  //CPU_Barrier();
  //std::cout << "SSE:" << _prof.CloseProfiler(prof) << std::endl;

  //shuffle(megatest);
  //int l2=0;
  //prof=_prof.OpenProfiler();
  //CPU_Barrier();
  //for(int i = 0; i < 1000000; i+=8)
  //{
  //  if(megatest[i]!=megatest[i+1]) {
  //    l2+=SGNCOMPARE(megatest[i],megatest[i+1]);
  //    continue;
  //  }
  //  if(megatest[i+2]!=megatest[i+3]) {
  //    l2+=SGNCOMPARE(megatest[i+2],megatest[i+3]);
  //    continue;
  //  }
  //  if(megatest[i+4]!=megatest[i+5]) {
  //    l2+=SGNCOMPARE(megatest[i+4],megatest[i+5]);
  //    continue;
  //  }
  //  if(megatest[i+6]!=megatest[i+7]) {
  //    l2+=SGNCOMPARE(megatest[i+6],megatest[i+7]);
  //    continue;
  //  }
  //}
  //CPU_Barrier();
  //std::cout << "NORMAL:" << _prof.CloseProfiler(prof) << std::endl;
  //TEST(l==l2);
  //}
  
  //float rotation,left,right,top,bottom,x,y;

  //float testfloats[7*10000];
  //for(int i = 0; i < 7*10000; ++i)
  //  testfloats[i]=0.1*i;

  //float res[4];
  //unsigned __int64 prof=_prof.OpenProfiler();
  //CPU_Barrier();
  //for(int cur=0; cur<70000; cur+=7)
  //{
  //  rotation=testfloats[cur];
  //  left=testfloats[cur+1];
  //  right=testfloats[cur+2];
  //  top=testfloats[cur+3];
  //  bottom=testfloats[cur+4];
  //  x=testfloats[cur+5];
  //  y=testfloats[cur+6];
  //}
  //CPU_Barrier();
  //std::cout << "NORMAL:" << _prof.CloseProfiler(prof) << std::endl;
  //std::cout << left << right << top << bottom << std::endl;
  ENDTEST;
}

TESTDEF::RETPAIR test_ALIASTABLE()
{
  BEGINTEST;
  double p[7] = { 0.1,0.2,0.3,0.05,0.05,0.15,0.15 };
  cAliasTable<unsigned int,double> a(p);
  uint counts[7] = {0};
  for(uint i = 0; i < 10000000; ++i)
    ++counts[a()];
  double real[7] = { 0.0 };
  for(uint i = 0; i < 7; ++i)
    real[i]=counts[i]/10000000.0;
  for(uint i = 0; i < 7; ++i)
    TEST(fcompare(p[i],real[i],(1LL<<44)));

  ENDTEST;
}

TESTDEF::RETPAIR test_ARRAYCIRCULAR()
{
  BEGINTEST;
  cArrayCircular<int> a;
  a.SetSize(25);
  TEST(a.Capacity()==25);
  for(int i = 0; i < 25; ++i)
    a.Push(i);
  TEST(a.Length()==25);
  
  TEST(a.Pop()==24);
  TEST(a.Pop()==23);
  a.Push(987);
  TEST(a.Pop()==987);
  TEST(a.Length()==23);
  a.Push(23);
  a.Push(24);
  for(int i = 0; i < 50; ++i)
    TEST(a[i]==(24-(i%25)));
  for(int i = 1; i < 50; ++i)
    TEST(a[-i]==((i-1)%25));
  a.SetSize(26);
  for(int i = 0; i < 25; ++i)
    TEST(a[i]==(24-(i%25)));
  a.Push(25); //This should overwrite 0
  TEST(a[0]==25);  
  TEST(a[-1]==0);  

  //const cArrayCircular<int>& b=a;
  //b[0]=5; // Should cause error

  ENDTEST;
}

template<bool SAFE> struct DEBUG_CDT_SAFE {};
template<> struct DEBUG_CDT_SAFE<false> {};
template<> struct DEBUG_CDT_SAFE<true>
{
  DEBUG_CDT_SAFE(const DEBUG_CDT_SAFE& copy) : __testret(*_testret) { isdead=this; }
  DEBUG_CDT_SAFE() : __testret(*_testret) { isdead=this; }
  ~DEBUG_CDT_SAFE() { TEST(isdead==this) }

  inline DEBUG_CDT_SAFE& operator=(const DEBUG_CDT_SAFE& right) { return *this; }
  
  static TESTDEF::RETPAIR* _testret;
  TESTDEF::RETPAIR& __testret;
  DEBUG_CDT_SAFE* isdead;
};
TESTDEF::RETPAIR* DEBUG_CDT_SAFE<true>::_testret=0;

template<bool SAFE=true>
struct DEBUG_CDT : DEBUG_CDT_SAFE<SAFE> {
  inline DEBUG_CDT(const DEBUG_CDT& copy) : _index(copy._index) { ++count; isdead=this; }
  inline DEBUG_CDT(int index=0) : _index(index) { ++count; isdead=this; }
  inline ~DEBUG_CDT() { //if(isdead!=this) throw "fail";
    --count; isdead=0; }

  inline DEBUG_CDT& operator=(const DEBUG_CDT& right) { _index=right._index; return *this; }
  inline bool operator<(const DEBUG_CDT& other) const { return _index<other._index; }
  inline bool operator>(const DEBUG_CDT& other) const { return _index>other._index; }
  inline bool operator<=(const DEBUG_CDT& other) const { return _index<=other._index; }
  inline bool operator>=(const DEBUG_CDT& other) const { return _index>=other._index; }
  inline bool operator==(const DEBUG_CDT& other) const { return _index==other._index; }
  inline bool operator!=(const DEBUG_CDT& other) const { return _index!=other._index; }

  static int count;
  DEBUG_CDT* isdead;
  int _index;
};
template<> int DEBUG_CDT<true>::count=0;
template<> int DEBUG_CDT<false>::count=0;

namespace bss_util { template<> struct ANI_IDTYPE<0> { typedef ANI_IDTYPE_TYPES<AniAttributeDiscrete<0>, cRefCounter*, cAutoRef<cRefCounter>, char> TYPES; }; }
namespace bss_util { template<> struct ANI_IDTYPE<1> { typedef ANI_IDTYPE_TYPES<AniAttributeInterval<1>, std::pair<cAutoRef<cRefCounter>, double>, std::pair<cAutoRef<cRefCounter>, double>, char, cRefCounter*> TYPES;
  static BSS_FORCEINLINE double toduration(TYPES::DATACONST p) { return p.second; } }; }
namespace bss_util { template<> struct ANI_IDTYPE<2> { typedef ANI_IDTYPE_TYPES<AniAttributeSmooth<2>, float> TYPES; }; }
namespace bss_util { template<> struct ANI_IDTYPE<3> { typedef ANI_IDTYPE_TYPES<AniAttributeGeneric<3>, std::function<void(void)>, std::function<void(void)>, char> TYPES; }; }

#include <memory>

typedef std::pair<cAutoRef<cRefCounter>, double> ANIOBJPAIR;

struct cAnimObj
{
  cAnimObj() : test(0), test2(0) {}
  int test;
  int test2;
  float fl;
  void BSS_FASTCALL donothing(cRefCounter*) { ++test; }
  cRefCounter* BSS_FASTCALL retnothing(ANIOBJPAIR p) {
    ++test2; 
    p.first->Grab(); 
    return (cRefCounter*)p.first; 
  }
  void BSS_FASTCALL remnothing(cRefCounter* p) { p->Drop(); }
  void BSS_FASTCALL setfloat(float a) { fl = a; }
  
  void BSS_FASTCALL TypeIDRegFunc(AniAttribute* p)
  {
    switch(p->typeID)
    {
    case 0:
      p->Attach(&AttrDefDiscrete<0>(delegate<void, cRefCounter*>::From<cAnimObj, &cAnimObj::donothing>(this)));
      break;
    case 1:
      p->Attach(&AttrDefInterval<1>(delegate<void, cRefCounter*>::From<cAnimObj, &cAnimObj::remnothing>(this), delegate<cRefCounter*, ANIOBJPAIR>::From<cAnimObj, &cAnimObj::retnothing>(this)));
      break;
    case 2:
      p->Attach(&AttrDefSmooth<2>(&fl, delegate<void, float>::From<cAnimObj, &cAnimObj::setfloat>(this)));
      break;
    case 3:
      p->Attach(0);
      break;
    }
  }
};

TESTDEF::RETPAIR test_ANIMATION()
{
  BEGINTEST;
  cRefCounter c;
  {
  c.Grab();
  cAnimation<StaticAllocPolicy<char>> a;
  a.Pause(true);
  a.SetTimeWarp(1.0);
  TEST(a.IsPaused());
  a.SetInterpolation<2>(&AniAttributeSmooth<2>::LerpInterpolate);
  a.AddKeyFrame<0>(KeyFrame<0>(0.0, &c));
  a.AddKeyFrame<0>(KeyFrame<0>(1.1, &c));
  a.AddKeyFrame<0>(KeyFrame<0>(2.0, &c));
  a.AddKeyFrame<1>(KeyFrame<1>(0.0, ANIOBJPAIR(&c,1.5)));
  a.AddKeyFrame<1>(KeyFrame<1>(1.0, ANIOBJPAIR(&c, 0.5)));
  a.AddKeyFrame<1>(KeyFrame<1>(1.5, ANIOBJPAIR(&c, 0.5)));
  a.AddKeyFrame<2>(KeyFrame<2>(0.0, 0.0f));
  a.AddKeyFrame<2>(KeyFrame<2>(1.0, 1.0f));
  a.AddKeyFrame<2>(KeyFrame<2>(2.0, 2.0f));
  a.Pause(false);
  TEST(!a.IsPaused());
  TEST(!a.HasTypeID(1));
  TEST(a.HasTypeID(0));
  TEST(a.GetTypeID(0)!=0);
  TEST(a.GetAnimationLength()==2.0);
  
  std::stringstream ss;
  a.Serialize(ss);
  
  cAnimation<StaticAllocPolicy<char>> aa;
  aa.Deserialize(ss);
  for(int i = 0; i<6; ++i) c.Grab(); // compensate for the pointer we just copied over

  cAnimObj obj;
  a.AddKeyFrame<3>(KeyFrame<3>(0.0, [&](){ c.Grab(); obj.test++; }));
  a.AddKeyFrame<3>(KeyFrame<3>(0.6, [&](){ c.Drop(); }));
  a.Attach(delegate<void,AniAttribute*>::From<cAnimObj,&cAnimObj::TypeIDRegFunc>(&obj));

  TEST(c.Grab()==14);
  c.Drop();
  TEST(obj.test==0);
  a.Start(0);
  TEST(a.IsPlaying());
  TEST(obj.test==2);
  a.Interpolate(0.5);
  TEST(obj.test==2);
  a.Interpolate(0.5);
  TEST(obj.test==2);
  a.Interpolate(0.5);
  TEST(obj.test==3);
  TEST(a.GetTimePassed()==1.5);
  a.Interpolate(0.5);
  TEST(a.GetTimePassed()==0.0);
  TEST(obj.test==4);
  a.Interpolate(0.5);
  TEST(obj.test==4);
  TEST(a.GetTimeWarp()==1.0);
  obj.test=0;
  a.Stop();
  TEST(!a.IsPlaying());
  a.Start(0);
  TEST(obj.test==2);
  a.Interpolate(0.6);
  obj.test=0;
  a.Stop();
  TEST(c.Grab()==15);
  c.Drop();
  c.Drop();
  TEST(!a.IsLooping());
  a.Loop(1.5,1.0);
  a.Interpolate(0.0);
  TEST(a.IsLooping());
  TEST(obj.test==3);
  a.Interpolate(3.0);
  a.Stop();
  TEST(c.Grab()==14);
  c.Drop();

  {
    cAnimation<StaticAllocPolicy<char>> b(a);
    obj.test=0;
    b.Attach(delegate<void, AniAttribute*>::From<cAnimObj, &cAnimObj::TypeIDRegFunc>(&obj));
    TEST(obj.test==0);
    b.Start(0);
    TEST(obj.test==4);
    TEST(c.Grab()==22);
    TEST(obj.test==2);
    b.Interpolate(10.0);
    TEST(obj.test==4);
    TEST(c.Grab()==20);
    c.Drop();
  }
  }
  TEST(c.Grab()==2);
  ENDTEST;
}
TESTDEF::RETPAIR test_ARRAYSIMPLE()
{
  BEGINTEST;

  WArray<int>::t a(5);
  TEST(a.Size()==5);
  a.Insert(5,2);
  TEST(a.Size()==6);
  TEST(a[2]==5);
  a.Remove(1);
  TEST(a[1]==5);
  a.SetSize(10);
  TEST(a[1]==5);
  TEST(a.Size()==10);

  {
  WArray<int>::t e(0);
  WArray<int>::t b(e);
  b=e;
  e.Insert(5,0);
  e.Insert(4,0);
  e.Insert(2,0);
  e.Insert(3,1);
  TEST(e.Size()==4);
  int sol[] = { 2,3,4,5 };
  TESTARRAY(sol,return e[i]==sol[i];);
  WArray<int>::t c(0);
  c=e;
  TESTARRAY(sol,return c[i]==sol[i];);
  WArray<int>::t d(0);
  e=d;
  TEST(!e.Size());
  e+=d;
  TEST(!e.Size());
  e=c;
  TESTARRAY(sol,return e[i]==sol[i];);
  e+=d;
  TESTARRAY(sol,return e[i]==sol[i];);
  d+=c;
  TESTARRAY(sol,return d[i]==sol[i];);
  e+=c;
  int sol2[] = { 2,3,4,5,2,3,4,5 };
  TESTARRAY(sol,return e[i]==sol[i];);
  }

  auto f = [](WArray<DEBUG_CDT<true>>::tSafe& arr)->bool{ 
    for(unsigned int i = 0; i < arr.Size(); ++i) 
      if(arr[i]._index!=i) 
        return false; 
    return true; 
  };
  auto f2 = [](WArray<DEBUG_CDT<true>>::tSafe& arr, unsigned int s){ for(unsigned int i = s; i < arr.Size(); ++i) arr[i]._index=i; };
  {
    DEBUG_CDT_SAFE<true>::_testret=&__testret;
    DEBUG_CDT<true>::count=0;
    WArray<DEBUG_CDT<true>>::tSafe b(10);
    f2(b,0);
    b.Remove(5);
    for(unsigned int i = 0; i < 5; ++i) TEST(b[i]._index==i);
    for(unsigned int i = 5; i < b.Size(); ++i) TEST(b[i]._index==(i+1));
    TEST(b.Size()==9);
    TEST(DEBUG_CDT<true>::count == 9);
    f2(b,0);
    b.SetSize(19);
    f2(b,9);
    TEST(f(b));
    TEST(DEBUG_CDT<true>::count == 19);
    TEST(b.Size()==19);
    WArray<DEBUG_CDT<true>>::tSafe c(b);
    TEST(f(c));
    TEST(DEBUG_CDT<true>::count == 38);
    b+=c;
    for(unsigned int i = 0; i < 19; ++i) TEST(b[i]._index==i);
    for(unsigned int i = 19; i < 38; ++i) TEST(b[i]._index==(i-19));
    TEST(DEBUG_CDT<true>::count == 57);
    b+c;
    f2(b,0);
    b.Insert(DEBUG_CDT<true>(), 5);
    for(unsigned int i = 0; i < 5; ++i) TEST(b[i]._index==i);
    for(unsigned int i = 6; i < b.Size(); ++i) TEST(b[i]._index==(i-1));
    TEST(DEBUG_CDT<true>::count == 58);
    b.Insert(DEBUG_CDT<true>(), b.Size());
    TEST(DEBUG_CDT<true>::count == 59);
  }
  TEST(!DEBUG_CDT<true>::count);
  
  auto f3 = [](WArray<DEBUG_CDT<false>>::tConstruct& arr)->bool{ 
    for(unsigned int i = 0; i < arr.Size(); ++i) 
      if(arr[i]._index!=i) 
        return false; 
    return true; 
  };
  auto f4 = [](WArray<DEBUG_CDT<false>>::tConstruct& arr, unsigned int s){ for(unsigned int i = s; i < arr.Size(); ++i) arr[i]._index=i; };
  {
    DEBUG_CDT<false>::count=0;
    WArray<DEBUG_CDT<false>>::tConstruct b(10);
    f4(b,0);
    b.Remove(5);
    for(unsigned int i = 0; i < 5; ++i) TEST(b[i]._index==i);
    for(unsigned int i = 5; i < b.Size(); ++i) TEST(b[i]._index==(i+1));
    TEST(b.Size()==9);
    TEST(DEBUG_CDT<false>::count == 9);
    f4(b,0);
    b.SetSize(19);
    f4(b,9);
    TEST(f3(b));
    TEST(DEBUG_CDT<false>::count == 19);
    TEST(b.Size()==19);
    WArray<DEBUG_CDT<false>>::tConstruct c(b);
    TEST(f3(c));
    TEST(DEBUG_CDT<false>::count == 38);
    b+=c;
    for(unsigned int i = 0; i < 19; ++i) TEST(b[i]._index==i);
    for(unsigned int i = 19; i < 38; ++i) TEST(b[i]._index==(i-19));
    TEST(DEBUG_CDT<false>::count == 57);
    b+c;
    f4(b,0);
    b.Insert(DEBUG_CDT<false>(), 5);
    for(unsigned int i = 0; i < 5; ++i) TEST(b[i]._index==i);
    for(unsigned int i = 6; i < b.Size(); ++i) TEST(b[i]._index==(i-1));
    TEST(DEBUG_CDT<false>::count == 58);
    b.Insert(DEBUG_CDT<false>(), b.Size());
    TEST(DEBUG_CDT<false>::count == 59);
  }
  TEST(!DEBUG_CDT<false>::count);

  ENDTEST;
}

struct FWDTEST {
  FWDTEST& operator=(const FWDTEST& right) { return *this; }
  FWDTEST& operator=(FWDTEST&& right) { return *this; }
};

TESTDEF::RETPAIR test_ARRAYSORT()
{
  BEGINTEST;
  
  DEBUG_CDT_SAFE<true>::_testret=&__testret; //If you don't do this it smashes the stack, but only sometimes, so it can create amazingly weird bugs.
  DEBUG_CDT<true>::count=0;

  {
  cArraySort<DEBUG_CDT<true>,CompT<DEBUG_CDT<true>>,unsigned int,cArraySafe<DEBUG_CDT<true>,unsigned int>> arrtest;
  arrtest.Insert(DEBUG_CDT<true>(0));
  arrtest.Insert(DEBUG_CDT<true>(1));
  arrtest.Insert(DEBUG_CDT<true>(2));
  arrtest.Remove(2);
  arrtest.Insert(DEBUG_CDT<true>(3));
  arrtest.Insert(DEBUG_CDT<true>(4));
  arrtest.Insert(DEBUG_CDT<true>(5));
  arrtest.Remove(0);
  arrtest.Insert(DEBUG_CDT<true>(6));
  arrtest.Remove(3);

  TEST(arrtest[0]==1);
  TEST(arrtest[1]==3);
  TEST(arrtest[2]==4);
  TEST(arrtest[3]==6);

  std::for_each(arrtest.begin(),arrtest.end(),[](DEBUG_CDT<true>& d) { d._index+=1; });
  TEST(arrtest[0]==2);
  TEST(arrtest[1]==4);
  TEST(arrtest[2]==5);
  TEST(arrtest[3]==7);

  cArraySort<DEBUG_CDT<true>,CompT<DEBUG_CDT<true>>,unsigned int,cArraySafe<DEBUG_CDT<true>,unsigned int>> arrtest2;
  arrtest2.Insert(DEBUG_CDT<true>(7));
  arrtest2.Insert(DEBUG_CDT<true>(8));
  arrtest=arrtest2;
  }
  TEST(!DEBUG_CDT<true>::count)

  ENDTEST;
}

int avltestnum[8];
BSS_FORCEINLINE bool AVLACTION(AVL_Node<int>* n) { static int c=0; avltestnum[c++]=n->_key; return false; }
BSS_FORCEINLINE AVL_Node<int>* LAVLCHILD(AVL_Node<int>* n) { return n->_left; }
BSS_FORCEINLINE AVL_Node<int>* RAVLCHILD(AVL_Node<int>* n) { return n->_right; }

TESTDEF::RETPAIR test_AVLTREE()
{
  BEGINTEST;

  FixedPolicy<AVL_Node<std::pair<int,int>>> fixedavl;
  cAVLtree<int, int,CompT<int>,FixedPolicy<AVL_Node<std::pair<int,int>>>> avlblah(&fixedavl);

  //unsigned __int64 prof=_prof.OpenProfiler();
  for(int i = 0; i<TESTNUM; ++i)
    avlblah.Insert(testnums[i],testnums[i]);
  //std::cout << _prof.CloseProfiler(prof) << std::endl;

  shuffle(testnums);
  //prof=_prof.OpenProfiler();
  uint c=0;
  for(int i = 0; i<TESTNUM; ++i)
    c+=(avlblah.GetRef(testnums[i])!=0);
  TEST(c==TESTNUM);
  //std::cout << _prof.CloseProfiler(prof) << std::endl;
  
  shuffle(testnums);
  //prof=_prof.OpenProfiler();
  for(int i = 0; i<TESTNUM; ++i)
    avlblah.Remove(testnums[i]);
  //std::cout << _prof.CloseProfiler(prof) << std::endl;
  avlblah.Clear();

  c=0;
  for(int i = 0; i<TESTNUM; ++i) // Test that no numbers are in the tree
    c+=(avlblah.GetRef(testnums[i])==0);
  TEST(c==TESTNUM);

  cAVLtree<int, std::pair<int,int>*>* tree = new cAVLtree<int, std::pair<int,int>*>();
  std::pair<int,int> test(5,5);
  tree->Insert(test.first,&test);
  tree->Get(test.first,0);
  tree->ReplaceKey(5,2);
  tree->Remove(test.first);
  tree->Clear();
  delete tree;

  //DEBUG_CDT_SAFE<false>::_testret=&__testret; // Set things up so we can ensure cAVLTree handles constructors/destructors properly.
  DEBUG_CDT<false>::count=0;

  {
    shuffle(testnums);
    cFixedAlloc<DEBUG_CDT<false>> dalloc(TESTNUM);
    typedef UqP_<DEBUG_CDT<false>,std::function<void(DEBUG_CDT<false>*)>> AVL_D;
    cAVLtree<int,AVL_D,CompT<int>,FixedPolicy<AVL_Node<std::pair<int,AVL_D>>>> dtree;
    for(int i = 0; i<TESTNUM; ++i)
    {
      auto dp = dalloc.alloc(1);
      new(dp) DEBUG_CDT<false>(testnums[i]);
      dtree.Insert(testnums[i],AVL_D(dp,[&](DEBUG_CDT<false>* p){p->~DEBUG_CDT<false>(); dalloc.dealloc(p);}));
    }

    shuffle(testnums);
    c=0;
    for(int i = 0; i<TESTNUM; ++i)
      c+=(dtree.GetRef(testnums[i])!=0);
    TEST(c==TESTNUM);
    
    shuffle(testnums);
    c=0;
    for(int i = 0; i<TESTNUM; ++i)
      c+=dtree.ReplaceKey(testnums[i],testnums[i]);
    TEST(c==TESTNUM);

    shuffle(testnums);
    c=0;
    AVL_D* r;
    for(int i = 0; i<TESTNUM; ++i)
    {
      if((r=dtree.GetRef(testnums[i]))!=0)
        c+=((*r->get())==testnums[i]);
    }
    TEST(c==TESTNUM);

    shuffle(testnums);
    for(int i = 0; i<TESTNUM; ++i)
      dtree.Remove(testnums[i]);

    c=0;
    for(int i = 0; i<TESTNUM; ++i)
      c+=(dtree.GetRef(testnums[i])==0);
    TEST(c==TESTNUM);
    TEST(!DEBUG_CDT<false>::count)
  }
  TEST(!DEBUG_CDT<false>::count)

  cAVLtree<int,void,CompT<int>,FixedPolicy<AVL_Node<int>>> avlblah2;

  //unsigned __int64 prof=_prof.OpenProfiler();
  for(int i = 0; i<TESTNUM; ++i)
    avlblah2.Insert(testnums[i]);
  //std::cout << _prof.CloseProfiler(prof) << std::endl;

  shuffle(testnums);
  //prof=_prof.OpenProfiler();
  c=0;
  for(int i = 0; i<TESTNUM; ++i)
    c+=((avlblah2.GetRef(testnums[i])!=0)&(avlblah2.Get(testnums[i],-1)==testnums[i]));
  TEST(c==TESTNUM);
  //std::cout << _prof.CloseProfiler(prof) << std::endl;
  
  shuffle(testnums);
  //prof=_prof.OpenProfiler();
  for(int i = 0; i<TESTNUM; ++i)
    avlblah2.Remove(testnums[i]);
  //std::cout << _prof.CloseProfiler(prof) << std::endl;
  avlblah2.Clear();

  avlblah2.Insert(1);
  avlblah2.Insert(4);
  avlblah2.Insert(3);
  avlblah2.Insert(5);
  avlblah2.Insert(2);
  avlblah2.Insert(-1);
  avlblah2.Insert(-2);
  avlblah2.Insert(-3);
  BreadthFirstTree<AVL_Node<int>,AVLACTION,LAVLCHILD,RAVLCHILD>(avlblah2.GetRoot(),8);
  TEST(avltestnum[0]==3);
  TEST(avltestnum[1]==4);
  TEST(avltestnum[2]==1);
  TEST(avltestnum[3]==5);
  TEST(avltestnum[4]==2);
  TEST(avltestnum[5]==-2);
  TEST(avltestnum[6]==-1);
  TEST(avltestnum[7]==-3);

  ENDTEST;
}

TESTDEF::RETPAIR test_BINARYHEAP()
{
  BEGINTEST;
  int a[] = { 7,33,55,7,45,1,43,4,3243,25,3,6,9,14,5,16,17,22,90,95,99,32 };
  int a3[] = { 7,33,55,7,45,1,43,4,3243,25,3,6,9,14,5,16,17,22,90,95,99,32 };
  int fill[] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  int a2[] = { 7,33,55,7,45,1,43,4,3243,25,3,6,9,14,5,16,17,22,90,95,99,32 };
  const int a2_SZ=sizeof(a2)/sizeof(int);

  auto arrtest = [&](int* a, int* b, size_t count){
    TESTCOUNTALL(count,a[i]==b[i]);
  };

  std::sort(std::begin(a2),std::end(a2));
  cBinaryHeap<int>::HeapSort(a3);
  arrtest(a2,a3,a2_SZ);

  std::sort(std::begin(a2),std::end(a2), [](int x, int y)->bool{ return x>y; });
  cBinaryHeap<int,unsigned int, CompTInv<int>>::HeapSort(a3);
  arrtest(a2,a3,a2_SZ);

  std::vector<int> b;
  cBinaryHeap<int> c;
  for(uint i = 0; i < a2_SZ; ++i)
  {
    b.push_back(a[i]);
    std::push_heap(b.begin(),b.end());
    c.Insert(a[i]);
    arrtest(&b[0],c,c.Length());
  }

  std::for_each(b.begin(),b.end(),[](int& a){ a+=1; });
  std::for_each(c.begin(),c.end(),[](int& a){ a+=1; });
  arrtest(&b[0],c,c.Length());

  for(uint i = 0; i < a2_SZ; ++i)
  {
    std::pop_heap(b.begin(),b.end()-i);
    c.Remove(0);

    //for(uint j = 0; j < c.Length(); ++j)
    //  fill[j]=c[j]; //Let's us visualize C's array
    //for(uint j = 0; j < c.Length(); ++j)
    //  assert(c[j]==b[j]);
    arrtest(&b[0],c,c.Length());
  }
  ENDTEST;
}

TESTDEF::RETPAIR test_BITARRAY()
{
  BEGINTEST;
  cBitArray<unsigned char> bits;
  bits.Clear();
  bits.SetSize(7);
  bits.SetBit(5,true);
  TEST(bits.GetRaw()[0]==32);
  TEST(bits.GetBits(0,6)==1);
  TEST(bits.GetBits(0,5)==0);
  bits+=5;
  TEST(bits.GetBits(0,6)==1);
  TEST(bits.GetBit(5));
  bits.SetBit(5,false);
  TEST(bits.GetRaw()[0]==0);
  TEST(bits.GetBits(0,6)==0);
  bits.SetBit(2,true);
  TEST(bits.GetRaw()[0]==4);
  TEST(bits.GetBit(2));
  bits+=0;
  TEST(bits.GetRaw()[0]==5);
  TEST(bits[0]);
  bits.SetBit(3,true);
  TEST(bits.GetRaw()[0]==13);
  TEST(bits.GetBits(0,6)==3);
  bits-=2;
  TEST(bits.GetRaw()[0]==9);
  TEST(bits.GetBits(0,6)==2);
  bits.SetSize(31);
  TEST(bits.GetRaw()[0]==9);
  bits[20]=true;
  TEST(bits.GetRaw()[0]==9);
  TEST(bits.GetRaw()[2]==16);
  TEST(bits.SetBit(30,true));
  TEST(bits[30]);
  TEST(!bits[31]);
  TEST(bits.GetRaw()[0]==9);
  TEST(bits.GetRaw()[2]==16);
  TEST(bits.GetRaw()[3]==64);
  TEST(!bits.SetBit(32,true));
  TEST(bits.GetBits(0,32)==4);
  bits[21]=false;
  TEST(bits.GetBits(0,32)==4);
  bits.SetBit(20,false);
  TEST(bits.GetRaw()[0]==9);
  TEST(bits.GetRaw()[2]==0);
  TEST(bits.GetRaw()[3]==64);
  TEST(bits.GetBits(0,32)==3);

  ENDTEST;
}

TESTDEF::RETPAIR test_BITFIELD()
{
  BEGINTEST;
  cBitField<uint> t;
  TEST(t==0);
  t[1]=true;
  TEST(t==1);
  t[2]=false;
  TEST(t==1);
  t[3]=t[1];
  TEST(t==3);
  t[2]=false;
  TEST(t==1);
  t[4]=true;
  TEST(t==5);
  t[8]=true;
  TEST(t==13);
  t=5;
  TEST(t==5);
  t+=3;
  TEST(t==7);
  t-=1;
  TEST(t==6);
  t-=3;
  TEST(t==4);
  t+=7;
  TEST(t==7);
  t-=4;
  TEST(t==3);
  t.Set(3,3);
  TEST(t==3);
  t.Set(5,-1);
  TEST(t==5);
  ENDTEST;
}

TESTDEF::RETPAIR test_BITSTREAM()
{
  BEGINTEST;

  std::stringstream s;
  {
    int a = -1;
    short b = 8199;
    char c = 31;
    char d = -63;
    cBitStream<std::ostream> so(s);
    so << a;
    so << b;
    so.Write(true);
    so.Write(&d, 7);
    so.Write(&d, 7);
    so.Write(true);
    so << c;
    so.Write(&c, 3);
    so.Write(&b, 14);
    so.Write(&c, 3);
    so << c;
  }
  {
    cBitStream<std::istream> si(s);
    int a;
    short b;
    char c;
    bool k;
    si >> a;
    TEST(a==-1);
    si >> b;
    TEST(b==8199);
    si >> k;
    TEST(k);
    c = 0;
    si.Read(&c, 7);
    TEST(c==65);
    c = 0;
    si.Read(&c, 7);
    TEST(c==65);
    si >> k;
    TEST(k);
    si >> c;
    TEST(c==31);
    c = 0;
    si.Read(&c, 3);
    TEST(c==7);
    b = 0;
    si.Read(&b, 14);
    TEST(b==8199);
    c = 0;
    si.Read(&c, 3);
    TEST(c==7);
    c = 0;
    si.Read<char>(c);
    TEST(c==31);
  }

  ENDTEST;
}

TESTDEF::RETPAIR test_BSS_QUEUE()
{
  BEGINTEST;
  cBSS_Queue<int> q(0);
  q.Clear();
  TEST(q.Capacity()==0);
  TEST(q.Empty());
  q.Push(1);
  TEST(q.Pop()==1);
  q.Push(2);
  q.Push(3);
  q.Push(4);
  TEST(q.Length()==3);
  TEST(q.Pop()==2);
  TEST(q.Length()==2);
  TEST(q.Pop()==3);
  TEST(q.Length()==1);
  q.Push(2);
  q.Push(3);
  q.Push(4);
  TEST(q.Length()==4);
  TEST(q.Pop()==4);
  TEST(q.Length()==3);
  TEST(q.Pop()==2);
  TEST(q.Length()==2);
  q.Clear();
  q.Push(5);
  TEST(q.Peek()==5);
  q.Push(6);
  TEST(q.Peek()==5);
  q.Discard();
  TEST(q.Length()==1);
  TEST(q.Peek()==6);
  cBSS_Queue<int> q2(3);
  q2.Push(7);
  TEST(q2.Peek()==7);
  q2=q;
  TEST(q2.Peek()==6);

  ENDTEST;
}

TESTDEF::RETPAIR test_BSS_STACK()
{
  BEGINTEST;
  cBSS_Stack<int> s(0);
  s.Clear();
  s.Push(1);
  TEST(s.Pop()==1);
  s.Push(2);
  s.Push(3);
  s.Push(4);
  TEST(s.Length()==3);
  TEST(s.Pop()==4);
  TEST(s.Length()==2);
  TEST(s.Pop()==3);
  TEST(s.Length()==1);
  s.Clear();
  s.Push(5);
  TEST(s.Peek()==5);
  TEST(s.Length()==1);
  cBSS_Stack<int> s2(3);
  s2.Push(6);
  TEST(s2.Peek()==6);
  s2=s;
  TEST(s2.Peek()==5);
  ENDTEST;
}

TESTDEF::RETPAIR test_CMDLINEARGS()
{
  BEGINTEST;
  cCmdLineArgs args("C:/fake/file/path.txt -r 2738 283.5 -aa 3 -noindice");
  args.Process([&__testret](const char* const* p, size_t n)
  {
    static cTrie<unsigned char> t(3,"-r","-aa","-noindice");
    switch(t[p[0]])
    {
    case 0:
      TEST(n==3);
      TEST(atof(p[1])==2738.0);
      TEST(atof(p[2])==283.5);
      break;
    case 1:
      TEST(n==2);
      TEST(atoi(p[1])==3);
      break;
    case 2:
      TEST(n==1);
      break;
    default:
      TEST(!strcmp(p[0],"C:/fake/file/path.txt"));
    }
  });
  ENDTEST;
}

TESTDEF::RETPAIR test_DISJOINTSET()
{
  BEGINTEST;

  std::pair<uint,uint> E[10]; // Initialize a complete tree with an arbitrary order.
  memset(E,0,sizeof(std::pair<uint,uint>)*10);
  E[0].first=1;
  E[1].first=2;
  E[2].first=3;
  E[3].first=4;
  E[4].first=2;
  E[5].first=3;
  E[6].first=4;
  E[7].first=3;
  E[8].first=4;
  E[9].first=4;
  E[4].second=1;
  E[5].second=1;
  E[6].second=1;
  E[7].second=2;
  E[8].second=2;
  E[9].second=3;
  shuffle(E); // shuffle our edges
  auto tree = cDisjointSet<uint>::MinSpanningTree(5,std::begin(E),std::end(E));
  TEST(tree.Size()==4);
  
  cDisjointSet<uint> s(5);
  s.Union(2,3);
  s.Union(2,4);
  s.Union(1,2);
  TEST((s.NumElements(3)==4));
  DYNARRAY(uint,elements,s.NumElements(3));
  TEST((s.GetElements(3,elements)==4));
  TEST(elements[0]==1);
  TEST(elements[1]==2);
  TEST(elements[2]==3);
  TEST(elements[3]==4);
  ENDTEST;
}

TESTDEF::RETPAIR test_DYNARRAY()
{
  BEGINTEST;
  cDynArray<cArraySimple<int>> x(0);
  TEST(!(int*)x);
  x.SetLength(5);
  x[0]=1;
  x[1]=2;
  x[2]=3;
  x[3]=4;
  x[4]=5;
  x.Add(6);
  TEST(x[5]==6);
  x.Add(7);
  TEST(x[5]==6);
  TEST(x[6]==7);
  x.Add(8);
  TEST(x[7]==8);
  x.Remove(5);
  TEST(x[5]==7);
  TEST(x[6]==8);
  x.Insert(6,5);
  TEST(x[5]==6);
  TEST(x[6]==7);
  cDynArray<cArraySimple<int>> y(3);
  y.Add(9);
  y.Add(10);
  y.Add(11);
  auto z = x+y;
  TEST(z.Length()==11);
  TEST(z[3]==4);
  TEST(z[8]==9);
  TEST(z[9]==10);
  TEST(z[10]==11);
  z+=y;
  TEST(z.Length()==14);
  TEST(z[3]==4);
  TEST(z[11]==9);
  TEST(z[12]==10);
  TEST(z[13]==11);

  cArbitraryArray<unsigned int> u(0);
  int ua[5] = { 1,2,3,4,5 };
  u.SetElement(ua);
  u.Get<int>(0)=1;
  u.Get<int>(1)=2;
  u.Get<int>(2)=3;
  u.Get<int>(3)=4;
  u.Get<int>(4)=5;
  u.Add(6);
  TEST(u.Get<int>(5)==6);
  u.Add(7);
  TEST(u.Get<int>(5)==6);
  TEST(u.Get<int>(6)==7);
  u.Add(8);
  TEST(u.Get<int>(7)==8);
  u.Remove(5);
  TEST(u.Get<int>(5)==7);
  TEST(u.Get<int>(6)==8);
  u.SetElement(7);
  TEST(u.Get<int>(5)==7);
  TEST(u.Get<int>(6)==8);

  ENDTEST;
}

TESTDEF::RETPAIR test_HIGHPRECISIONTIMER()
{
  BEGINTEST;
  cHighPrecisionTimer timer;
  timer.Update();
  double ldelta=timer.GetDelta();
  double ltime=timer.GetTime();
  TEST(ldelta>0.0);
  TEST(ltime>0.0);
  TEST(ldelta<1000.0); //If that took longer than a second, either your CPU choked, or something went terribly wrong.
  TEST(ltime<1000.0);
  timer.Update(std::numeric_limits<double>::infinity());
  TEST(timer.GetDelta()==0.0);
  TEST(timer.GetTime()==ltime);
  timer.Update(0.5);
  timer.ResetDelta();
  TEST(timer.GetDelta()==0.0);
  TEST(timer.GetTime()>0.0);
  timer.Update();
  timer.ResetTime();
  TEST(timer.GetDelta()>0.0);
  TEST(timer.GetTime()==0.0);
  auto prof = timer.OpenProfiler();
  TEST(prof!=0);
  TEST(timer.CloseProfiler(prof)<1000);
  ENDTEST;
}

TESTDEF::RETPAIR test_IDHASH()
{
  BEGINTEST;

  {
    cIDHash<int> hash(3);
    TEST(hash.Length()==0);
    TEST(hash.MaxID()==0);
    int a = hash.Add(5);
    int b = hash.Add(6);
    int c = hash.Add(7);
    int d = hash.Add(8);
    TEST(hash.Length()==4);
    TEST(hash.MaxID()==3);
    TEST(a==0);
    TEST(b==1);
    TEST(c==2);
    TEST(d==3);
    TEST(hash[a]==5);
    TEST(hash[b]==6);
    TEST(hash[c]==7);
    TEST(hash.Get(d)==8);
    hash.Remove(b);
    TEST(hash.Length()==3);
    b = hash.Add(9);
    TEST(hash.Length()==4);
    TEST(hash.MaxID()==3);
    TEST(b==1);
    TEST(hash[b]==9);
    cIDHash<int> hash2(hash);
    TEST(hash2.Length()==4);
    TEST(hash2.MaxID()==3);
    TEST(hash2[b]==9);
    cIDHash<int> hash3(std::move(hash));
    TEST(hash3.Length()==4);
    TEST(hash3.MaxID()==3);
    TEST(hash3[b]==9);
  }

  {
    cIDReverse<int,unsigned int, StaticAllocPolicy<int>,-1> hash;
    int a = hash.Add(1);
    int b = hash.Add(2);
    int c = hash.Add(3);
    int d = hash.Add(4);
    int e = hash.Add(5);
    int f = hash.Add(6);
    int g = hash.Add(7);
    int h = hash.Add(8);
    TEST(hash.Length()==8);
    TEST(hash.MaxID()==7);
    for(int i = 0; i<8; ++i) TEST(hash[i]==(i+1));
    for(int i = 0; i<8; ++i) TEST(hash.Lookup(i+1)==i);
    hash.Remove(b);
    hash.Remove(d);
    hash.Remove(e);
    TEST(hash.Length()==5);
    TEST(hash.MaxID()==7);
    hash.Compress();
    TEST(hash.Length()==5);
    TEST(hash.MaxID()==4);
    TEST(hash[0]==1);
    TEST(hash[1]==8);
    TEST(hash[2]==3);
    TEST(hash[3]==7);
    TEST(hash[4]==6);
    TEST(hash.Lookup(1)==0);
    TEST(hash.Lookup(2)==(unsigned int)-1);
    TEST(hash.Lookup(3)==2);
    TEST(hash.Lookup(4)==(unsigned int)-1);
    TEST(hash.Lookup(5)==(unsigned int)-1);
    TEST(hash.Lookup(6)==4);
    TEST(hash.Lookup(7)==3);
    TEST(hash.Lookup(8)==1);
  }
  ENDTEST;
}

TESTDEF::RETPAIR test_SCHEDULER()
{
  BEGINTEST;
  bool ret[3]={false,false,true};
  cScheduler<std::function<double()>> s;
  s.Add(0.0,[&]()->double { ret[0]=true; return 0.0; });
  s.Add(0.0,[&]()->double { ret[1]=true; return 10000000.0; });
  s.Add(10000000.0,[&]()->double { ret[2]=false; return 0.0; });
  TEST(s.Length()==3);
  s.Update();
  TEST(ret[0]);
  TEST(ret[1]);
  TEST(ret[2]);
  TEST(s.Length()==2);
  ENDTEST;
}

TESTDEF::RETPAIR test_INIPARSE()
{
  BEGINTEST;
  ENDTEST;
}

#define INI_E(s,k,v,nk,ns) TEST(!ini.EditEntry(MAKESTRING(s),MAKESTRING(k),MAKESTRING(v),nk,ns))
#define INI_NE(s,k,v,nk,ns) TEST(ini.EditEntry(MAKESTRING(s),MAKESTRING(k),MAKESTRING(v),nk,ns)<0)
#define INI_R(s,k,nk,ns) TEST(!ini.EditEntry(MAKESTRING(s),MAKESTRING(k),0,nk,ns))
#define INI_G(s,k,nk,ns) TEST(!ini.EditEntry(MAKESTRING(s),MAKESTRING(k),MAKESTRING(v),nk,ns))

TESTDEF::RETPAIR test_INISTORAGE()
{
  BEGINTEST;
  
  cINIstorage ini("inistorage.ini");

  auto fn=[&](cINIentry* e, const char* s, int i) -> bool { return (e!=0 && !strcmp(e->GetString(),s) && e->GetInt()==i); };
  auto fn2=[&](const char* s) {
    FILE* f;
    FOPEN(f,"inistorage.ini","rb"); //this will create the file if it doesn't already exist
    TEST(f!=0);
    if(f!=0)
    {
      fseek(f,0,SEEK_END);
      size_t size=(size_t)ftell(f);
      fseek(f,0,SEEK_SET);
      cStr ini(size+1);
      size=fread(ini.UnsafeString(),sizeof(char),size,f); //reads in the entire file
      ini.UnsafeString()[size]='\0';
      fclose(f);
      TEST(!strcmp(ini,s));
    }  
  };
  auto fn3=[&]() {
    ini.AddSection("1");
    INI_E(1,a,1,-1,0);
    INI_E(1,a,2,-1,0);
    INI_E(1,a,3,-1,0);
    INI_E(1,a,4,-1,0);
    INI_E(1,b,1,-1,0);
    ini.AddSection("2");
    INI_E(2,a,1,-1,0);
    INI_E(2,a,2,-1,0);
    INI_E(2,b,1,-1,0);
    ini.AddSection("2");
    INI_E(2,a,1,-1,1);
    INI_E(2,a,2,-1,1);
    INI_E(2,b,1,-1,1);
    INI_E(1,c,1,-1,0); // We do these over here to test adding things into the middle of the file
    INI_E(1,c,2,-1,0);
    INI_E(1,d,1,-1,0);
    ini.AddSection("2");
  };
  auto fn4=[&](const char* s, unsigned int index) -> bool {
    cINIsection* sec=ini.GetSection(s,index);
    return sec!=0 && sec->GetIndex()==index;
  };

  fn3();

  TEST(fn(ini.GetEntryPtr("1","a",0,0),"1",1));
  TEST(fn(ini.GetEntryPtr("1","a",1,0),"2",2));
  TEST(fn(ini.GetEntryPtr("1","a",2,0),"3",3));
  TEST(fn(ini.GetEntryPtr("1","a",3,0),"4",4));
  TEST(!ini.GetEntryPtr("1","a",4,0));
  TEST(fn(ini.GetEntryPtr("1","b",0,0),"1",1));
  TEST(!ini.GetEntryPtr("1","b",1,0));
  TEST(fn(ini.GetEntryPtr("1","c",0,0),"1",1));
  TEST(fn(ini.GetEntryPtr("1","c",1,0),"2",2));
  TEST(!ini.GetEntryPtr("1","c",2,0));
  TEST(fn(ini.GetEntryPtr("1","d",0,0),"1",1));
  TEST(!ini.GetEntryPtr("1","d",1,0));
  TEST(fn(ini.GetEntryPtr("2","a",0,0),"1",1));
  TEST(fn(ini.GetEntryPtr("2","a",1,0),"2",2));
  TEST(!ini.GetEntryPtr("2","a",2,0));
  TEST(fn(ini.GetEntryPtr("2","b",0,0),"1",1));
  TEST(!ini.GetEntryPtr("2","b",1,0));
  TEST(fn(ini.GetEntryPtr("2","a",0,1),"1",1));
  TEST(fn(ini.GetEntryPtr("2","a",1,1),"2",2));
  TEST(!ini.GetEntryPtr("2","a",2,1));
  TEST(fn(ini.GetEntryPtr("2","b",0,1),"1",1));
  TEST(!ini.GetEntryPtr("2","b",1,1));
  TEST(!ini.GetEntryPtr("2","a",0,2));
  TEST(!ini.GetEntryPtr("2","b",0,2));
  TEST(ini.GetNumSections("1")==1);
  TEST(ini.GetNumSections("2")==3);
  TEST(ini.GetNumSections("3")==0);
  TEST(ini.GetNumSections("")==0);
  TEST(ini["1"].GetNumEntries("a")==4);
  TEST(ini["1"].GetNumEntries("b")==1);
  TEST(ini["1"].GetNumEntries("asdf")==0);
  TEST(ini["1"].GetNumEntries("")==0);

  ini.EndINIEdit();
  fn2("[1]\na=1\na=2\na=3\na=4\nb=1\nc=1\nc=2\nd=1\n\n[2]\na=1\na=2\nb=1\n\n[2]\na=1\na=2\nb=1\n\n[2]");

  int valid=0;
  for(auto i = ini.begin(); i != ini.end(); ++i)
    for(auto j = i->begin(); j != i->end(); ++j)
      valid+=j->IsValid();
  
  TEST(valid==14);

  INI_E(1,a,8,3,0); // Out of order to try and catch any bugs that might result from that
  INI_E(1,a,6,1,0);
  INI_E(1,a,7,2,0);
  INI_E(1,a,5,0,0);
  INI_NE(1,a,9,4,0);
  INI_E(1,b,2,0,0);
  INI_NE(1,b,9,1,0);
  INI_E(1,c,3,0,0); // Normal in order attempt
  INI_E(1,c,4,1,0);
  INI_NE(1,c,9,2,0);
  INI_E(1,d,2,0,0);
  INI_NE(1,d,9,1,0);
  INI_E(2,a,4,1,0); // out of order
  INI_E(2,a,3,0,0); 
  INI_NE(2,a,9,2,0);
  INI_E(2,b,2,0,0);
  INI_NE(2,b,9,1,0);
  INI_E(2,a,3,0,1); // in order
  INI_E(2,a,4,1,1); 
  INI_NE(2,a,9,2,1);
  INI_E(2,b,2,0,1);
  INI_NE(2,b,9,1,1);
  INI_NE(2,a,9,0,2);
  INI_NE(2,b,9,0,2);
  
  TEST(fn(ini.GetEntryPtr("1","a",0,0),"5",5));
  TEST(fn(ini.GetEntryPtr("1","a",1,0),"6",6));
  TEST(fn(ini.GetEntryPtr("1","a",2,0),"7",7));
  TEST(fn(ini.GetEntryPtr("1","a",3,0),"8",8));
  TEST(!ini.GetEntryPtr("1","a",4,0));
  TEST(fn(ini.GetEntryPtr("1","b",0,0),"2",2));
  TEST(!ini.GetEntryPtr("1","b",1,0));
  TEST(fn(ini.GetEntryPtr("1","c",0,0),"3",3));
  TEST(fn(ini.GetEntryPtr("1","c",1,0),"4",4));
  TEST(!ini.GetEntryPtr("1","c",2,0));
  TEST(fn(ini.GetEntryPtr("1","d",0,0),"2",2));
  TEST(!ini.GetEntryPtr("1","d",1,0));
  TEST(fn(ini.GetEntryPtr("2","a",0,0),"3",3));
  TEST(fn(ini.GetEntryPtr("2","a",1,0),"4",4));
  TEST(!ini.GetEntryPtr("2","a",2,0));
  TEST(fn(ini.GetEntryPtr("2","b",0,0),"2",2));
  TEST(!ini.GetEntryPtr("2","b",1,0));
  TEST(fn(ini.GetEntryPtr("2","a",0,1),"3",3));
  TEST(fn(ini.GetEntryPtr("2","a",1,1),"4",4));
  TEST(!ini.GetEntryPtr("2","a",2,1));
  TEST(fn(ini.GetEntryPtr("2","b",0,1),"2",2));
  TEST(!ini.GetEntryPtr("2","b",1,1));
  TEST(!ini.GetEntryPtr("2","a",0,2));
  TEST(!ini.GetEntryPtr("2","b",0,2));

  ini.EndINIEdit();
  fn2("[1]\na=5\na=6\na=7\na=8\nb=2\nc=3\nc=4\nd=2\n\n[2]\na=3\na=4\nb=2\n\n[2]\na=3\na=4\nb=2\n\n[2]");

  INI_R(1,a,1,0);
  TEST(fn(ini.GetEntryPtr("1","a",0,0),"5",5));
  TEST(fn(ini.GetEntryPtr("1","a",1,0),"7",7));
  TEST(fn(ini.GetEntryPtr("1","a",2,0),"8",8));
  TEST(!ini.GetEntryPtr("1","a",3,0));
  TEST(fn(ini.GetEntryPtr("1","b",0,0),"2",2));
  INI_R(1,a,2,0);
  TEST(fn(ini.GetEntryPtr("1","a",0,0),"5",5));
  TEST(fn(ini.GetEntryPtr("1","a",1,0),"7",7));
  TEST(!ini.GetEntryPtr("1","a",2,0));
  TEST(fn(ini.GetEntryPtr("1","b",0,0),"2",2));
  INI_R(1,a,0,0);
  TEST(fn(ini.GetEntryPtr("1","a",0,0),"7",7));
  TEST(!ini.GetEntryPtr("1","a",1,0));
  TEST(fn(ini.GetEntryPtr("1","b",0,0),"2",2));
  INI_R(1,c,0,0);
  TEST(fn(ini.GetEntryPtr("1","c",0,0),"4",4));
  INI_R(1,d,0,0);
  TEST(!ini.GetEntryPtr("1","d",0,0));
  INI_R(1,a,0,0);
  TEST(!ini.GetEntryPtr("1","a",0,0));
  TEST(fn(ini.GetEntryPtr("1","b",0,0),"2",2));

  INI_R(2,b,0,0);
  TEST(fn(ini.GetEntryPtr("2","a",0,0),"3",3));
  TEST(fn(ini.GetEntryPtr("2","a",1,0),"4",4));
  TEST(!ini.GetEntryPtr("2","a",2,0));
  TEST(!ini.GetEntryPtr("2","b",0,0));
  INI_R(2,a,1,1);
  TEST(fn(ini.GetEntryPtr("2","a",0,1),"3",3));
  TEST(!ini.GetEntryPtr("2","a",1,1));
  TEST(!ini.GetEntryPtr("2","b",1,1));
  INI_R(2,a,0,1);
  TEST(!ini.GetEntryPtr("2","a",0,1));
  TEST(!ini.GetEntryPtr("2","b",1,1));
  INI_R(2,b,0,1);
  TEST(!ini.GetEntryPtr("2","a",0,1));
  TEST(!ini.GetEntryPtr("2","b",0,1));
  ini.RemoveSection("2",0);
  TEST(fn4("2",0)); // Catches index decrementing errors
  TEST(fn4("2",1));
  ini.RemoveSection("2",1);

  ini.EndINIEdit();
  fn2("[1]\nb=2\nc=4\n\n[2]\n\n");
  
  fn3();
  ini.EndINIEdit();
  fn2("[1]\nb=2\nc=4\na=1\na=2\na=3\na=4\nb=1\nc=1\nc=2\nd=1\n\n[2]\na=1\na=2\nb=1\n\n[1]\n\n[2]\na=1\na=2\nb=1\n\n[2]\n\n[2]");

  cStr comp;
  for(auto i = ini.Front(); i!=0; i=i->next)
  {
    comp=comp+"\n["+i->val.GetName()+']';
    for(auto j = i->val.Front(); j!=0; j=j->next)
      comp=comp+'\n'+j->val.GetKey()+'='+j->val.GetString();
  }
  
  // Due to organizational optimizations these come out in a slightly different order than in the INI, depending on when they were added.
#ifdef BSS_COMPILER_GCC
  TEST(!strcmp(comp,"\n[1]\nb=2\nb=1\nc=4\na=1\na=2\na=3\na=4\nc=1\nc=2\nd=1\n[1]\n[2]\n[2]\na=1\na=2\nb=1\n[2]\na=1\na=2\nb=1\n[2]"));
#else
  TEST(!strcmp(comp,"\n[1]\nb=2\nb=1\nc=4\nc=1\nc=2\na=1\na=2\na=3\na=4\nd=1\n[1]\n[2]\na=1\na=2\nb=1\n[2]\na=1\na=2\nb=1\n[2]\n[2]"));
#endif

  TEST(!remove("inistorage.ini"));
  ENDTEST;
}

struct KDtest {
  float rect[4];
  LLBase<int> list;
  KDNode<KDtest>* node;
  static int hits;
};
int KDtest::hits=0;
BSS_FORCEINLINE const float* BSS_FASTCALL KDtest_RECT(KDtest* t) { return t->rect; }
BSS_FORCEINLINE LLBase<KDtest>& BSS_FASTCALL KDtest_LIST(KDtest* t) { return *(LLBase<KDtest>*)&t->list; }
BSS_FORCEINLINE void BSS_FASTCALL KDtest_ACTION(KDtest* t) { ++KDtest::hits; }
BSS_FORCEINLINE KDNode<KDtest>*& BSS_FASTCALL KDtest_NODE(KDtest* t) { return t->node; }

TESTDEF::RETPAIR test_KDTREE()
{
  BEGINTEST;
  FixedPolicy<KDNode<KDtest>> alloc;
  cKDTree<KDtest,FixedPolicy<KDNode<KDtest>>,&KDtest_RECT,&KDtest_LIST,&KDtest_ACTION,&KDtest_NODE> tree;
  KDtest r1 = { 0,0,1,1,0,0 };
  KDtest r2 = { 1,1,2,2,0,0 };
  KDtest r3 = { 0,0,2,2,0,0 };
  KDtest r4 = { 0.5,0.5,1.5,1.5,0,0 };
  KDtest r5 = { 0.5,0.5,0.75,0.75,0,0 };
  tree.Insert(&r1);
  tree.Insert(&r2);
  tree.Remove(&r1);
  tree.Insert(&r1);
  tree.Insert(&r3);
  tree.Insert(&r4);
  tree.Remove(&r4);
  tree.Insert(&r5);
  tree.Insert(&r4);
  tree.Clear();
  tree.InsertRoot(&r1);
  tree.InsertRoot(&r2);
  tree.InsertRoot(&r3);
  tree.InsertRoot(&r4);
  tree.InsertRoot(&r5);
  tree.Solve();
  float c1[4] = {-1,-1,-0.5,-0.5};
  tree.Traverse(c1);
  TEST(KDtest::hits==0);
  float c2[4] = {-1,-1,0,0};
  tree.Traverse(c2);
  TEST(KDtest::hits==2);
  tree.Remove(&r1);
  tree.Remove(&r2);
  tree.Remove(&r3);
  tree.Remove(&r4);
  tree.Remove(&r5);
  tree.Traverse(c1);
  TEST(tree.GetRoot()->num==0);
  ENDTEST;
}
TESTDEF::RETPAIR test_KHASH()
{
  BEGINTEST;
  //cKhash<int, char,false,KH_INT_HASHFUNC,KH_INT_EQUALFUNC<int>,KH_INT_VALIDATEPTR<int>> hashtest;
  //hashtest.Insert(21354,0);
  //hashtest.Insert(34623,0);
  //hashtest.Insert(52,0);
  //hashtest.Insert(1,0);
  //int r=hashtest.GetIterKey(hashtest.GetIterator(1));
  cKhash_Int<bss_Log*> hasherint;
  hasherint.Insert(25, &_failedtests);
  hasherint.Get(25);
  hasherint.Remove(25);
  cKhash_StringIns<bss_Log*> hasher;
  hasher.Insert("", &_failedtests);
  hasher.Insert("Video", (bss_Log*)5);
  hasher.SetSize(100);
  hasher.Insert("Physics",0);
  bss_Log* check = hasher.Get("Video");
  check = hasher.Get("Video");
  //unsigned __int64 diff = _prof.CloseProfiler(ID);

  cKhash_Pointer<short,const void*,false> set;
  set.Insert(0,1);
  set.Insert(&check,1);
  set.Insert(&hasher,1);
  set.Insert(&hasherint,1);
  set.Insert(&set,1);
  set.GetKey(0);
  TEST(set.Exists(0));
  ENDTEST;
}

TESTDEF::RETPAIR test_LINKEDARRAY()
{
  BEGINTEST;
  cLinkedArray<int> _arr;
  uint a = _arr.Add(4);
  _arr.Reserve(2);
  uint b=_arr.InsertAfter(6,a);
  _arr.InsertBefore(5,b);
  TEST(_arr.Length()==3);
  int v[]={4,5,6};
  uint c=0;
  for(auto i=_arr.begin(); i!=_arr.end(); ++i)
    TEST(*i==v[c++]);
  _arr.Remove(b);
  TEST(_arr.Length()==2);
  TEST(_arr[a]==4);
  TEST(*_arr.GetItemPtr(a)==4);
  c=0;
  for(auto i=_arr.begin(); i!=_arr.end(); ++i)
    TEST(*i==v[c++]);
  _arr.Clear();
  TEST(!_arr.Length());
  ENDTEST;

}

template<bool L, bool S>
bool cmplist(cLinkedList<int, StandardAllocPolicy<cLLNode<int>>, L, S>& list, const char* nums)
{
  auto cur = list.begin();
  bool r=true;
  while(cur.IsValid() && *nums!=0 && r)
    r=(*(cur++)==(*(nums++) - '0'));
  return r;
}

TESTDEF::RETPAIR test_LINKEDLIST()
{
  BEGINTEST;
  cLinkedList<int, StandardAllocPolicy<cLLNode<int>>, true, true> test;
  cLLNode<int>* llp[5];

  llp[0] = test.Add(1);
  TEST(cmplist(test,"1"));
  llp[1] = test.Add(2);
  TEST(cmplist(test,"12"));
  llp[3] = test.Add(4);
  TEST(cmplist(test,"124"));
  llp[4] = test.Add(5);
  TEST(cmplist(test,"1245"));
  llp[2] = test.Insert(3,llp[3]);
  TEST(cmplist(test,"12345"));
  test.Remove(llp[3]);
  TEST(cmplist(test,"1235"));
  test.Remove(llp[0]);
  TEST(cmplist(test,"235"));
  test.Remove(llp[4]);
  TEST(cmplist(test,"23"));
  test.Insert(0,0);
  TEST(cmplist(test,"230"));
  TEST(test.Length()==3);
  
  cLinkedList<int, StandardAllocPolicy<cLLNode<int>>, false, true> test2;

  llp[0] = test2.Add(1);
  TEST(cmplist(test2,"1"));
  llp[1] = test2.Add(2);
  TEST(cmplist(test2,"21"));
  llp[3] = test2.Add(4);
  TEST(cmplist(test2,"421"));
  llp[4] = test2.Add(5);
  TEST(cmplist(test2,"5421"));
  llp[2] = test2.Insert(3,llp[1]);
  TEST(cmplist(test2,"54321"));
  test2.Remove(llp[3]);
  TEST(cmplist(test2,"5321"));
  test2.Remove(llp[0]);
  TEST(cmplist(test2,"532"));
  test2.Remove(llp[4]);
  TEST(cmplist(test2,"32"));
  test2.Insert(0,0);
  TEST(cmplist(test2,"032"));
  TEST(test2.Length()==3);

  cLinkedList<int, StandardAllocPolicy<cLLNode<int>>> test3;

  llp[0] = test3.Add(1);
  TEST(cmplist(test3,"1"));
  llp[1] = test3.Add(2);
  TEST(cmplist(test3,"21"));
  llp[3] = test3.Add(4);
  TEST(cmplist(test3,"421"));
  llp[4] = test3.Add(5);
  TEST(cmplist(test3,"5421"));
  llp[2] = test3.Insert(3,llp[1]);
  TEST(cmplist(test3,"54321"));
  test3.Remove(llp[3]);
  TEST(cmplist(test3,"5321"));
  test3.Remove(llp[0]);
  TEST(cmplist(test3,"532"));
  test3.Remove(llp[4]);
  TEST(cmplist(test3,"32"));
  test3.Insert(0,0);
  TEST(cmplist(test3,"032"));

  ENDTEST;
}

std::atomic<unsigned int> lq_c;
unsigned short lq_end[TESTNUM];
std::atomic<unsigned short> lq_pos;

template<class T>
void _locklessqueue_consume(void* p)
{
  while(!startflag);
  T* q = (T*)p;
  uint c;
  while((c = lq_pos.fetch_add(1, std::memory_order_relaxed))<TESTNUM) {
    while(!q->Pop(lq_end[c]));
  }
}

template<class T>
void _locklessqueue_produce(void* p)
{
  while(!startflag);
  T* q = (T*)p;
  unsigned int c;
  while((c = lq_c.fetch_add(1, std::memory_order_relaxed))<=TESTNUM) {
    q->Push(c);
  }
}

TESTDEF::RETPAIR test_LOCKLESSQUEUE()
{
  BEGINTEST;
  {
  cLocklessQueue<__int64> q; // Basic sanity test
  q.Push(5);
  __int64 c;
  TEST(q.Pop(c));
  TEST(c==5);
  TEST(!q.Pop(c));
  TEST(c==5);
  q.Push(4);
  q.Push(3);
  TEST(q.Pop(c));
  TEST(c==4);
  q.Push(2);
  q.Push(1);
  TEST(q.Pop(c));
  TEST(c==3);
  TEST(q.Pop(c));
  TEST(c==2);
  TEST(q.Pop(c));
  TEST(c==1);
  TEST(!q.Pop(c));
  TEST(c==1);
  }

  const int NUMTHREADS=18;
  std::thread threads[NUMTHREADS];

  //typedef cLocklessQueue<unsigned int,true,true,size_t,size_t> LLQUEUE_SCSP; 
  typedef cLocklessQueue<unsigned short, size_t> LLQUEUE_SCSP;
  {
  LLQUEUE_SCSP q; // single consumer single producer test
  unsigned __int64 ppp=_prof.OpenProfiler();
  lq_c=1;
  lq_pos=0;
  memset(lq_end, 0, sizeof(short)*TESTNUM);
  startflag=false;
  threads[0] = std::thread(_locklessqueue_produce<LLQUEUE_SCSP>, &q);
  threads[1] = std::thread(_locklessqueue_consume<LLQUEUE_SCSP>, &q);
  startflag=true;
  threads[0].join();
  threads[1].join();
  //std::cout << '\n' << _prof.CloseProfiler(ppp) << std::endl;
  bool check=true;
  for(int i = 0; i < TESTNUM;++i)
    check=check&&(lq_end[i]==i+1);
  TEST(check);
  }

  for(int k= 0; k < 1; ++k) {
    typedef cMicroLockQueue<unsigned short, size_t> LLQUEUE_MCMP;
    for(int j = 2; j<=NUMTHREADS; j=fbnext(j)) {
      lq_c = 1;
      lq_pos = 0;
      memset(lq_end, 0, sizeof(short)*TESTNUM);
      LLQUEUE_MCMP q;   // multi consumer multi producer test
      startflag=false;
      //threads[0] = std::thread(_locklessqueue_consume<LLQUEUE_MCMP>, &q);
      //for(int i=1; i<j; ++i)
      //  threads[i] = std::thread(_locklessqueue_produce<LLQUEUE_MCMP>, &q);
      for(int i=0; i<j; ++i)
        threads[i] = std::thread((i&1)?_locklessqueue_produce<LLQUEUE_MCMP>:_locklessqueue_consume<LLQUEUE_MCMP>, &q);
      startflag=true;
      for(int i = 0; i<j; ++i)
        threads[i].join();

      std::sort(std::begin(lq_end), std::end(lq_end));
      bool check=true;
      for(int i = 0; i < TESTNUM-1; ++i) {
        check = check&&(lq_end[i]==i+1);
      }
      TEST(check);

      //std::cout << '\n' << j << " threads: " << q.GetContentions() << std::endl;
    }
  }

  ENDTEST;
}


TESTDEF::RETPAIR test_MAP()
{
  BEGINTEST;
  cMap<int,uint> test;
  test.Clear();
  int ins[] = { 0,5,6,237,289,12,3 };
  int get[] = { 0,3,5,6,12 };
  uint res[] = { 0,6,1,2,5 };
  uint count=0;
  TESTARRAY(ins,return test.Insert(ins[i],count++)!=-1;);
  std::sort(std::begin(ins),std::end(ins));
  for(unsigned int i = 0; i < test.Length(); ++i)
  { TEST(test.KeyIndex(i)==ins[i]); }
  for(int i = 0; i < sizeof(get)/sizeof(int); ++i)
  { TEST(test[test.Get(get[i])]==res[i]); }

  TEST(test.Remove(0)==0);
  TEST(test.Get(0)==-1);
  TEST(test.Length()==((sizeof(ins)/sizeof(int))-1));
  
#ifndef BSS_COMPILER_GCC // Once again, GCC demonstrates its amazing ability to NOT DEFINE ANY FUCKING CONSTRUCTORS
  cMap<int,FWDTEST> tst;
  tst.Insert(0,FWDTEST());
  FWDTEST lval;
  tst.Insert(1,lval);
#endif
  ENDTEST;
}

TESTDEF::RETPAIR test_PRIORITYQUEUE()
{
  BEGINTEST;
  cPriorityQueue<int,cStr,CompTInv<int>,uint,cArraySafe<std::pair<int,cStr>,uint>> q;

  q.Push(5,"5");
  q.Push(3,"3");
  q.Push(3,"3");
  q.Push(6,"6");
  q.Push(1,"1");
  q.Push(1,"1");
  q.Push(1,"1");
  q.Push(2,"2");
  
  TEST(q.Get(1).first==2);
  TEST(q.Get(2).first==1);
  TEST(q.Peek().first==1);
  TEST(q.Peek().second=="1");
  TEST(q.Pop().first==1);
  TEST(q.Pop().first==1);
  TEST(q.Pop().first==1);
  TEST(q.Pop().first==2);
  TEST(q.Pop().first==3);
  TEST(q.Pop().first==3);

  q.Push(1,"1");
  q.Push(2,"2");
  q.Push(4,"4");

  TEST(q.Get(0).first==q.Peek().first)
  q.Discard();
  TEST(!q.Empty());
  TEST(q.Pop().first==2);
  TEST(q.Pop().first==4);
  TEST(q.Pop().first==5);
  TEST(q.Pop().first==6);
  TEST(q.Empty());

  ENDTEST;
}

TESTDEF::RETPAIR test_RATIONAL()
{
  BEGINTEST;
  cRational<int> tr(1,10);
  cRational<int> tr2(1,11);
  cRational<int> tr3(tr+tr2);
  TEST(tr.N()==1 && tr.D()==10);
  TEST(tr2.N()==1 && tr2.D()==11);
  TEST(tr3.N()==21 && tr3.D()==110);
  tr3=(tr-tr2);
  TEST(tr3.N()==1 && tr3.D()==110);
  tr3=(tr*tr2);
  TEST(tr3.N()==1 && tr3.D()==110);
  tr3=(tr/tr2);
  TEST(tr3.N()==11 && tr3.D()==10);
  tr3=(tr+3);
  TEST(tr3.N()==31 && tr3.D()==10);
  tr3=(tr-3);
  TEST(tr3.N()==-29 && tr3.D()==10);
  tr3=(tr*3);
  TEST(tr3.N()==3 && tr3.D()==10);
  tr3=(tr/3);
  TEST(tr3.N()==1 && tr3.D()==30);
  TEST((tr<3));
  TEST(!(tr>3));
  TEST(!(tr<tr2));
  TEST((tr>tr2));
  TEST(!(tr==3));
  TEST((tr!=3));
  TEST(!(tr==tr2));
  TEST((tr!=tr2));
  ENDTEST;
}

TESTDEF::RETPAIR test_TRBTREE()
{
  BEGINTEST;
  FixedPolicy<TRB_Node<int>> fixedalloc;
  cTRBtree<int, CompT<int>, FixedPolicy<TRB_Node<int>>> blah(&fixedalloc);

  shuffle(testnums);
  for(int i = 0; i<TESTNUM; ++i)
    blah.Insert(testnums[i]);

  TEST(!blah.Get(-1))
  TEST(!blah.Get(TESTNUM+1))

  shuffle(testnums);
  int num=0;
  for(int i = 0; i<TESTNUM; ++i)
  {
    auto p=blah.Get(testnums[i]);
    if(p!=0)
      num+=(p->value==testnums[i]);
  }
  TEST(num==TESTNUM);
  
  blah.Insert(1);
  blah.Insert(1);
  blah.Insert(2);
  int last=-1;
  uint pass=0;
  uint same=0;
  for(TRB_Node<int>* pnode=blah.Front(); pnode!=0; pnode=pnode->next)
  {
    if(pnode->value==last)
      same+=1;
    if(pnode->value<last)
      pass+=1;
    last=pnode->value;
  }
  TEST(!pass);
  TEST(same==3);
  
  blah.Remove(1);
  blah.Remove(1);
  blah.Remove(2);

  shuffle(testnums);
  num=0;
  int n2=0;
  int n3=0;
  for(int i = 0; i<TESTNUM; ++i)
  {
    num+=(blah.Get(testnums[i])!=0);
    n2+=blah.Remove(testnums[i]);
    n3+=(!blah.Get(testnums[i]));
  }
  TEST(num==TESTNUM);
  TEST(n2==TESTNUM);
  TEST(n3==TESTNUM);

  //std::cout << _prof.CloseProfiler(prof) << std::endl;
  ENDTEST;
}

struct REF_TEST : cRefCounter
{
  REF_TEST(TESTDEF::RETPAIR& t) : __testret(t) {}
  ~REF_TEST() { TEST(true); }
  virtual void DestroyThis() { TEST(true); cRefCounter::DestroyThis(); }

  TESTDEF::RETPAIR& __testret;
};

TESTDEF::RETPAIR test_REFCOUNTER()
{
  BEGINTEST;
  cRefCounter a;
  TEST(a.Grab()==1);
  cRefCounter b(a);
  TEST(b.Grab()==1);
  REF_TEST* c = new REF_TEST(__testret);
  c->Grab();
  c->Drop();
  ENDTEST;
}

#define INSTANTIATE_SETTINGS
#include "cSettings.h"

DECL_SETGROUP(0,"main");
DECL_SETTING(0,0,float,0.0f,"ANIME");
DECL_SETTING(0,1,int,0,"MANGA");
DECL_SETTING(0,2,double,0.0,"pow");
DECL_SETGROUP(1,"submain");
DECL_SETTING(1,0,float,15.0f,"zip");
DECL_SETTING(1,1,int,5,"poofers");
DECL_SETTING(1,2,std::vector<cStr>,std::vector<cStr>(),"lots");
DECL_SETTING(1,3,std::vector<__int64>,std::vector<__int64>(),"intlots");

TESTDEF::RETPAIR test_SETTINGS()
{
  BEGINTEST;
  cSettingManage<1,0>::LoadAllFromINI(cINIstorage("test.ini"));
  cSettingManage<1,0>::SaveAllToINI(cINIstorage("test.ini"));
  ENDTEST;
}

struct SINGLETEST : cSingleton<SINGLETEST>
{
  SINGLETEST() : cSingleton<SINGLETEST>(this) {}
};

TESTDEF::RETPAIR test_SINGLETON()
{
  BEGINTEST;
  TEST(SINGLETEST::Instance()==0);
  {
  SINGLETEST test;
  TEST(SINGLETEST::Instance()==&test);
  }
  TEST(SINGLETEST::Instance()==0);
  SINGLETEST* test2;
  {
  SINGLETEST test;
  TEST(SINGLETEST::Instance()==&test);
  test2 = new SINGLETEST();
  TEST(SINGLETEST::Instance()==test2);
  }
  TEST(SINGLETEST::Instance()==test2);
  delete test2;
  TEST(SINGLETEST::Instance()==0);
  ENDTEST;
}

TESTDEF::RETPAIR test_STR()
{
  BEGINTEST;
  cStr s("blah");
  TEST(!strcmp(s,"blah"));
  cStr s2(std::move(s));
  TEST(!strcmp(s.c_str(),""));
  TEST(!strcmp(s2,"blah"));
  cStr s3(s2);
  TEST(!strcmp(s3,"blah"));
  s3=std::move(s2);
  cStr s4;
  s4=s3;
  TEST(!strcmp(s4,"blah"));
  TEST(!strcmp(s4+' ',"blah "));
  TEST(!strcmp(s4+" ","blah "));
  TEST(!strcmp(s4+"","blah"));
  TEST(!strcmp(s4+s3,"blahblah"));
  s4+=' ';
  TEST(!strcmp(s4,"blah "));
  s4+=" a";
  TEST(!strcmp(s4,"blah  a"));
  s4+="";
  TEST(!strcmp(s4,"blah  a"));
  s4+=s3;
  TEST(!strcmp(s4,"blah  ablah"));
  TEST(!strcmp(cStr(0,"1 2",' '),"1"));
  TEST(!strcmp(cStr(1,"1 2",' '),"2"));
  TEST(!strcmp(cStrF("%s2","place"),"place2"));
  TEST(!strcmp(cStrF("2","place"),"2"));
#ifdef BSS_PLATFORM_WIN32 // We can only run this test meaningfully on windows, because its the only one where it actually makes a difference.
  TEST(!strcmp(cStr(BSS__L("Törkylempijävongahdus")),"TÃ¶rkylempijÃ¤vongahdus"));
#endif

  s4.GetChar(6)='b';
  TEST(!strcmp(s4,"blah  bblah"));
  s4.GetChar(0)='a';
  TEST(!strcmp(s4,"alah  bblah"));
  s4.resize(80);
  s4.GetChar(60)='2';
  s4.RecalcSize();
  TEST(s4.size()==11);
  s3="  \n  trim  \n";
  TEST(!strcmp(s3.Trim(),"trim"));
  s3=" \n \n  trim";
  TEST(!strcmp(s3.Trim(),"trim"));
  s3="trim \n ";
  TEST(!strcmp(s3.Trim(),"trim"));
  s3="trim";
  TEST(!strcmp(s3.Trim(),"trim"));
  TEST(!strcmp(s3.ReplaceChar('r','x'),"txim"));
  TEST(!strcmp(cStr::StripChar(s3,'t'),"xim"));
  TEST(!strcmp(cStr::StripChar(s3,'x'),"tim"));
  TEST(!strcmp(cStr::StripChar(s3,'m'),"txi"));
  
  auto a = cStr::Explode(' ', "lots of words");
  TEST(a.size()==3);
  TEST(!strcmp(a[0],"lots"));
  TEST(!strcmp(a[1],"of"));
  TEST(!strcmp(a[2],"words"));

#ifdef BSS_COMPILER_MSC
  TEST(!strcmp(s2.c_str(),"")); // This is only supposed to happen on VC++, other compilers don't have to do this (GCC in particular doesn't).
#endif
  cStr sdfderp(s+cStr("temp")+cStr("temp")+cStr("temp")+cStr("temp"));
  
  std::vector<int> vec1;
  cStr::ParseTokens<int>("",",",vec1,&atoi);
  TEST(vec1.size()==0);
  cStr::ParseTokens<int>("1234",",",vec1,&atoi);
  TEST(vec1.size()==1);
  TEST(vec1[0]==1234);
  vec1.clear();
  cStr::ParseTokens<int>("1234,235,2,6,1,0,,39,ahjs",",",vec1,&atoi);
  TEST(vec1.size()==8);
  TEST(vec1[0]==1234);
  TEST(vec1[1]==235);
  TEST(vec1[2]==2);
  TEST(vec1[3]==6);
  TEST(vec1[4]==1);
  TEST(vec1[5]==0);
  TEST(vec1[6]==39);
  TEST(vec1[7]==0);
  vec1.clear();
#ifdef BSS_PLATFORM_WIN32
  cStrW::ParseTokens<int>(L"",L",",vec1,&_wtoi);
  TEST(vec1.size()==0);
  cStrW::ParseTokens<int>(L"1234,235,2,6,1,0,,39,ahjs",L",",vec1,&_wtoi);
  TEST(vec1.size()==8);
  TEST(vec1[0]==1234);
  TEST(vec1[1]==235);
  TEST(vec1[2]==2);
  TEST(vec1[3]==6);
  TEST(vec1[4]==1);
  TEST(vec1[5]==0);
  TEST(vec1[6]==39);
  TEST(vec1[7]==0);
  vec1.clear();
#endif
  cStr::ParseTokens<int>("1234,235,2,6,1,0,,39,ahjs",",",vec1,[](const char* s)->int{ return atoi(s)+1; });
  TEST(vec1.size()==8);
  TEST(vec1[0]==1235);
  TEST(vec1[1]==236);
  TEST(vec1[2]==3);
  TEST(vec1[3]==7);
  TEST(vec1[4]==2);
  TEST(vec1[5]==1);
  TEST(vec1[6]==40);
  TEST(vec1[7]==1);

  cStrT<int> u32("jkl");
  TEST(u32[0]=='j');
  TEST(u32[1]=='k');
  TEST(u32[2]=='l');
  ENDTEST;
}

TESTDEF::RETPAIR test_STRTABLE()
{
  BEGINTEST;

  const int SZ = sizeof(PANGRAMS)/sizeof(const bsschar*);
  cStr pangrams[SZ];
  const char* pstr[SZ];
  for(uint i = 0; i < SZ; ++i)
    pstr[i]=(pangrams[i]=PANGRAMS[i]).c_str();

  cStrTable<char> mbstable(pstr,SZ);
  cStrTable<bsschar> wcstable(PANGRAMS,SZ);
  cStrTable<char> mbstable2(pstr,6);

  for(unsigned int i = 0; i < mbstable.Length(); ++i)
    TEST(!strcmp(mbstable[i],pstr[i]));

  mbstable+=mbstable2;
  mbstable.AppendString("append");
  mbstable+="append2";

  for(int i = 0; i < SZ; ++i)
    TEST(!strcmp(mbstable[i],pstr[i]));
  for(int i = 0; i < 6; ++i)
    TEST(!strcmp(mbstable[i+SZ],pstr[i]));
  TEST(!strcmp(mbstable[SZ+6],"append"));
  TEST(!strcmp(mbstable[SZ+7],"append2"));

  std::fstream fs;
  fs.open("dump.txt",std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
  mbstable.DumpToStream(&fs);
  fs.close();
  fs.open("dump.txt",std::ios_base::in | std::ios_base::binary);
  cStrTable<char> ldtable(&fs,(size_t)bssFileSize("dump.txt"));
  for(int i = 0; i < SZ; ++i)
    TEST(!strcmp(ldtable[i],pstr[i]));
  for(int i = 0; i < 6; ++i)
    TEST(!strcmp(ldtable[i+SZ],pstr[i]));

  mbstable2=ldtable;
  for(int i = 0; i < SZ; ++i)
    TEST(!strcmp(mbstable2[i],pstr[i]));
  for(int i = 0; i < 6; ++i)
    TEST(!strcmp(mbstable2[i+SZ],pstr[i]));

  ENDTEST;
}

TESTDEF::RETPAIR test_THREAD()
{
  BEGINTEST;
  cHighPrecisionTimer timer;
  unsigned __int64 m;
  cThread t([](unsigned __int64& m, cHighPrecisionTimer& timer){cThread::Wait(); m=timer.CloseProfiler(m); }, std::ref(m), std::ref(timer));
  TEST(t.join(2)==-1);
  m=timer.OpenProfiler();
  t.Signal();
  TEST(t.join(1000)!=(size_t)-1);
  std::cout << "\n" << m << std::endl;
  //while(i > 0)
  //{
  //  //for(int j = RANDINTGEN(50000,100000); j > 0; --j) { std::this_thread::sleep_for(0); useless.Update(); }
  //  //timer.Update();
  //  apc.SendSignal(); // This doesn't work on linux
  //} 

  ENDTEST;
}

TESTDEF::RETPAIR test_TRIE()
{
  BEGINTEST;
  const char* strs[] = { "fail","on","tex","rot","ro","ti","ontick","ondestroy","te","tick" };
  cTrie<unsigned char> t(9,"tick","on","tex","rot","ro","ti","ontick","ondestroy","te","tick");
  cTrie<unsigned char> t2(9,strs);
  cTrie<unsigned char> t3(strs);
  TEST(t3["fail"]==0);
  TEST(t3["tick"]==9);
  
  //cStr randcstr[200];
  //const char* randstr[200];
  //for(uint i = 0; i < 200; ++i)
  //{
  //  for(uint j = RANDINTGEN(2,20); j>0; --j)
  //    randcstr[i]+=(char)RANDINTGEN('a','z');
  //  randstr[i]=randcstr[i];
  //}
  //cTrie<unsigned int> t(50,randstr);
  //cKhash_String<unsigned char> hashtest;
  //for(uint i = 0; i < 50; ++i)
  //  hashtest.Insert(randstr[i],i);
  //unsigned int dm;
  //shuffle(testnums);
  //auto prof = _prof.OpenProfiler();
  //CPU_Barrier();
  //for(uint i = 0; i < TESTNUM; ++i)
  //  //dm=hashtest[randstr[testnums[i]%200]];
  //  dm=t[randstr[testnums[i]%200]];
  //  //dm=t[strs[testnums[i]%10]];
  //CPU_Barrier();
  //auto res = _prof.CloseProfiler(prof);
  //std::cout << dm << "\nTIME:" << res << std::endl;
  
  for(uint i = 0; i < 9; ++i)
  {
    switch(t[strs[i]]) // Deliberatly meant to test for one failure
    {
    case 0:
      TEST(false); break;
    case 1:
      TEST(i==1); break;
    case 2:
      TEST(i==2); break;
    case 3:
      TEST(i==3); break;
    case 4:
      TEST(i==4); break;
    case 5:
      TEST(i==5); break;
    case 6:
      TEST(i==6); break;
    case 7:
      TEST(i==7); break;
    case 8:
      TEST(i==8); break;
    default:
      TEST(i==0);
    }
  }
  TEST(t[strs[9]]==0);

  ENDTEST;
}

struct foobar
{
  void BSS_FASTCALL nyan(unsigned int cat) { TEST(cat==5); }
  void BSS_FASTCALL nyannyan(int cat, int kitty) { TEST(cat==2); TEST(kitty==-3); }
  void BSS_FASTCALL nyannyannyan(int cat, int kitty, bool fluffy) { TEST(cat==-6); TEST(kitty==0); TEST(fluffy); }
  void BSS_FASTCALL zoidberg() { TEST(true); }
  void nothing() {}
  void nothing2() {}
  void nothing3() {}

  TESTDEF::RETPAIR& __testret;
};

struct fooref : public DEBUG_CDT<false>, cRefCounter {};

TESTDEF::RETPAIR test_SMARTPTR()
{
  BEGINTEST;

  cOwnerPtr<char> p(new char[56]);
  char* pp=p;
  cOwnerPtr<char> p2(p);
  cOwnerPtr<char> p3(std::move(p));
  TEST(pp==p3);

  {
    DEBUG_CDT<false>::count=0;
  cAutoRef<fooref> p4(new fooref());
  cAutoRef<fooref> p5(p4);
  cAutoRef<fooref> p6(std::move(p5));
  p5=p6;
  p6=std::move(p4);
  }
  TEST(!DEBUG_CDT<false>::count);

  ENDTEST;
}

TESTDEF::RETPAIR test_DELEGATE()
{
  BEGINTEST;
  foobar foo = { __testret };
  auto first = delegate<void>::From<foobar,&foobar::zoidberg>(&foo);
  auto second = delegate<void,unsigned int>::From<foobar,&foobar::nyan>(&foo);
  auto three = delegate<void,int,int>::From<foobar,&foobar::nyannyan>(&foo);
  auto four = delegate<void,int,int,bool>::From<foobar,&foobar::nyannyannyan>(&foo);
  
  delegate<void> copy(first);
  copy=first;
  CPU_Barrier();
  copy();
  CPU_Barrier();
  delegate<void,unsigned int> copy2(second);
  CPU_Barrier();
  copy2(5);
  CPU_Barrier();
  three(2,-3);
  four(-6,0,true);
  ENDTEST;
}

TESTDEF::RETPAIR test_LLBASE()
{
  BEGINTEST;
  ENDTEST;
}//*/

TESTDEF::RETPAIR test_LOCKLESS()
{
  BEGINTEST;
  CPU_Barrier();

  {//Sanity checks for atomic_inc
  int a = 1;
  atomic_xadd(&a);
  TEST(a==2);
  CPU_Barrier();
  int* b=&a;
  atomic_xadd(b);
  CPU_Barrier();
  TEST(a==3);
  volatile int* c=&a;
  atomic_xadd<int>(c);
  atomic_xadd<int>(c=b);
  CPU_Barrier();
  TEST(a==5);
  }
  {//Sanity checks for atomic_xchg
  int a = 1;
  int b = 2;
  b=atomic_xchg<int>(&a,b);
  TEST(a==2);
  TEST(b==1);
  atomic_xchg<int>(&b,a);
  TEST(a==2);
  TEST(b==2);
  int* c=&a;
  atomic_xchg<int>(c,3);
  TEST(a==3);
  volatile int* d=&b;
  a=atomic_xchg<int>(d,5);
  TEST(a==2);
  TEST(b==5);
  }
  ENDTEST;
}

//void tttest(float a[4][4], float** b)
//{
//  if(a!=0)
//    a[0][0]=1.0f;
//}

TESTDEF::RETPAIR test_OS()
{
  BEGINTEST;
  TEST(FolderExists("../bin")||FolderExists("bin"));
#ifdef BSS_PLATFORM_WIN32
  TEST(FolderExists(BSS__L("C:/windows/")));
#else
  TEST(FolderExists(BSS__L("/usr/")));
#endif
  TEST(!FolderExists("abasdfwefs"));
  TEST(!FolderExists(BSS__L("abasdfwefs/alkjsdfs/sdfjkd/alkjsdfs/sdfjkd/alkjsdfs/sdfjkd/")));
  FILE* f;
  FOPEN(f,"blank.txt","w+");
  fclose(f);
  TEST(FileExists("blank.txt"));
  TEST(FileExists(BSS__L("blank.txt")));
  TEST(!FileExists("testaskdjlhfs.sdkj"));
  TEST(!FileExists(BSS__L("testaskdjlhfs.sdkj")));
  //TEST(FileExists("testlink"));
  //TEST(FileExists(BSS__L("testlink")));
  //TEST(FolderExists("IGNORE/symlink/"));
  //TEST(FolderExists(BSS__L("IGNORE/symlink/")));
  //{
  //std::unique_ptr<char[],bssdll_delete<char[]>> p = FileDialog(true,0,BSS__L("test"));
  //}

//#ifdef BSS_PLATFORM_WIN32
//  SetRegistryValue(HKEY_LOCAL_MACHINE,"SOFTWARE\\test","valcheck","data");
//  SetRegistryValue(HKEY_LOCAL_MACHINE,"SOFTWARE\\test\\test","valcheck","data");
//  DelRegistryNode(HKEY_LOCAL_MACHINE,"SOFTWARE\\test");
//#endif
  //AlertBox("test", "title", 0x00000010L);
  
  //float at[4][4];
  //at[0][0]=(float)(size_t)f;
  //CPU_Barrier();
  //tttest(0,&at);
  //CPU_Barrier();
  ENDTEST;
}

TESTDEF::RETPAIR test_STREAMSPLITTER()
{
  BEGINTEST;
  std::stringstream ss1;
  std::stringstream ss2;
  std::stringstream ss3;
  ss1 << "1 ";
  ss2 << "2 ";
  ss3 << "3 ";

  StreamSplitter splitter;
  std::ostream split(&splitter);
  splitter.AddTarget(&ss1);
  split << "a ";
  splitter.AddTarget(&ss2);
  split << "b ";
  splitter.AddTarget(&ss3);
  split << "c " << std::flush;
  splitter.ClearTargets();
  split << "d " << std::flush;
  splitter.AddTarget(&ss1);
  split << "e " << std::flush;

  TEST(ss1.str()=="1 a b c e ");
  TEST(ss2.str()=="2 b c ");
  TEST(ss3.str()=="3 c ");
  ENDTEST;
}

/*void subleq_computer(int[] mem)
{
int c=0;
int b;
while(c>=0)
{
b=mem[c+1];
mem[b] = mem[b]-mem[mem[c]];
c=(mem[b]>0)?(c+3):mem[c+2];
}
}*/

// --- Begin main testing function ---

int main(int argc, char** argv)
{
  PROFILE_FUNC();
  PROFILE_UPDATE();
  PROFILE_OUTPUT("./memtest.txt");

  ForceWin64Crash();
  SetWorkDirToCur();
  unsigned int seed=(unsigned int)time(NULL);
  srand(seed);
  
  for(int i = 0; i<TESTNUM; ++i)
    testnums[i]=i;
  shuffle(testnums);

  TESTDEF tests[] = {
    { "bss_util_c.h", &test_bss_util_c },
    { "bss_util.h", &test_bss_util },
    { "bss_Log.h", &test_bss_LOG },
    { "bss_algo.h", &test_bss_algo },
    { "bss_alloc_additive.h", &test_bss_ALLOC_ADDITIVE },
    { "bss_alloc_fixed.h", &test_bss_ALLOC_FIXED },
    { "bss_alloc_fixed_MT.h", &test_bss_ALLOC_FIXED_LOCKLESS },
    { "bss_depracated.h", &test_bss_deprecated },
    { "bss_dual.h", &test_bss_DUAL },
    { "bss_fixedpt.h", &test_bss_FIXEDPT },
    { "bss_graph.h", &test_bss_GRAPH },
    { "bss_sse.h", &test_bss_SSE },
    { "cAliasTable.h", &test_ALIASTABLE },
    { "cAnimation.h", &test_ANIMATION },
    { "cArrayCircular.h", &test_ARRAYCIRCULAR },
    { "cArraySimple.h", &test_ARRAYSIMPLE },
    { "cArraySort.h", &test_ARRAYSORT },
    { "cAVLtree.h", &test_AVLTREE },
    { "cBinaryHeap.h", &test_BINARYHEAP },
    { "cBitArray.h", &test_BITARRAY },
    { "cBitField.h", &test_BITFIELD },
    { "cBitStream.h", &test_BITSTREAM },
    { "cBSS_Queue.h", &test_BSS_QUEUE },
    { "cBSS_Stack.h", &test_BSS_STACK },
    { "cCmdLineArgs.h", &test_CMDLINEARGS },
    { "cDisjointSet.h", &test_DISJOINTSET },
    { "cDynArray.h", &test_DYNARRAY },
    { "cHighPrecisionTimer.h", &test_HIGHPRECISIONTIMER },
    { "cIDHash.h", &test_IDHASH },
    { "cScheduler.h", &test_SCHEDULER },
    //{ "INIparse.h", &test_INIPARSE },
    { "cINIstorage.h", &test_INISTORAGE },
    { "cKDTree.h", &test_KDTREE },
    { "cKhash.h", &test_KHASH },
    { "cLinkedArray.h", &test_LINKEDARRAY },
    { "cLinkedList.h", &test_LINKEDLIST },
    { "lockless.h", &test_LOCKLESS },
    { "cLocklessQueue.h", &test_LOCKLESSQUEUE },
    { "cMap.h", &test_MAP },
    { "cPriorityQueue.h", &test_PRIORITYQUEUE },
    { "cRational.h", &test_RATIONAL },
    { "cRefCounter.h", &test_REFCOUNTER },
    { "cSettings.h", &test_SETTINGS },
    { "cSingleton.h", &test_SINGLETON },
    { "cStr.h", &test_STR },
    { "cStrTable.h", &test_STRTABLE },
    { "cThread.h", &test_THREAD },
    { "cTRBtree.h", &test_TRBTREE },
    { "cTrie.h", &test_TRIE },
    { "cSmartPtr.h", &test_SMARTPTR },
    { "delegate.h", &test_DELEGATE },
    //{ "LLBase.h", &test_LLBASE },*/
    { "os.h", &test_OS },
    { "cStreamSplitter.h", &test_STREAMSPLITTER },
  };

  const size_t NUMTESTS=sizeof(tests)/sizeof(TESTDEF);

  std::cout << "Black Sphere Studios - Utility Library v" << (uint)BSSUTIL_VERSION.Major << '.' << (uint)BSSUTIL_VERSION.Minor << '.' <<
    (uint)BSSUTIL_VERSION.Revision << ": Unit Tests\nCopyright (c)2013 Black Sphere Studios\n" << std::endl;
  const int COLUMNS[3] = { 24, 11, 8 };
  printf("%-*s %-*s %-*s\n",COLUMNS[0],"Test Name", COLUMNS[1],"Subtests", COLUMNS[2],"Pass/Fail");

  TESTDEF::RETPAIR numpassed;
  std::vector<uint> failures;
  for(uint i = 0; i < NUMTESTS; ++i)
  {
    numpassed=tests[i].FUNC(); //First is total, second is succeeded
    if(numpassed.first!=numpassed.second) failures.push_back(i);

    printf("%-*s %*s %-*s\n",COLUMNS[0],tests[i].NAME, COLUMNS[1],cStrF("%u/%u",numpassed.second,numpassed.first).c_str(), COLUMNS[2],(numpassed.first==numpassed.second)?"PASS":"FAIL");
  }

  if(failures.empty())
    std::cout << "\nAll tests passed successfully!" << std::endl;
  else
  {
    std::cout << "\nThe following tests failed (seed = " << seed << "): " << std::endl;
    for (uint i = 0; i < failures.size(); i++)
      std::cout << "  " << tests[failures[i]].NAME << std::endl;
    std::cout << "\nThese failures indicate either a misconfiguration on your system, or a potential bug. Please report all bugs to http://code.google.com/p/bss-util/issues/list\n\nA detailed list of failed tests was written to failedtests.txt" << std::endl;
  }

  std::cout << "\nPress Enter to exit the program." << std::endl;
  std::cin.get();

  return 0;
}

// --- The rest of this file is archived dead code ---



/*struct OBJSWAP_TEST {
  unsigned int i;
  bool operator==(const OBJSWAP_TEST& j) const { return i==j.i; }
  bool operator!=(const OBJSWAP_TEST& j) const { return i!=j.i; }
};

TESTDEF::RETPAIR test_OBJSWAP()
{
  BEGINTEST;
  
  unsigned int vals[] = { 0,1,2,3,4,5 };
  const char* strs[] = { "001", "002", "003", "004", "005" };
  unsigned int* zp=vals+0;
  unsigned int* zp2=vals+1;
  unsigned int* zp3=vals+2;
  unsigned int* zp4=vals+3;
  unsigned int* zp5=vals+4;
  OBJSWAP_TEST o[6] = { {1},{2},{3},{4},{5},{6} };
  for(uint i = 0; i < 5; ++i)
  {
    switch(PSWAP(vals+i,5,zp,zp2,zp3,zp4,zp5))
    {
    case 0:
      TEST(i==0); break;
    case 1:
      TEST(i==1); break;
    case 2:
      TEST(i==2); break;
    case 3:
      TEST(i==3); break;
    case 4:
      TEST(i==4); break;
    default:
      TEST(false);
    }

    switch(cObjSwap<const bsschar*>(PANGRAMS[i],5,PANGRAMS[4],PANGRAMS[3],PANGRAMS[2],PANGRAMS[1],PANGRAMS[0]))
    {
    case 0:
      TEST(i==4); break;
    case 1:
      TEST(i==3); break;
    case 2:
      TEST(i==2); break;
    case 3:
      TEST(i==1); break;
    case 4:
      TEST(i==0); break;
    default:
      TEST(false);
    }

    switch(STRSWAP(strs[i],5,"000","002","003","004","005")) // Deliberaly meant to test for one failure
    {
    case 0:
      TEST(false); break;
    case 1:
      TEST(i==1); break;
    case 2:
      TEST(i==2); break;
    case 3:
      TEST(i==3); break;
    case 4:
      TEST(i==4); break;
    default:
      TEST(i==0);
    }

    switch(cObjSwap<OBJSWAP_TEST>(o[i],5,o[0],o[1],o[5],o[3],o[4])) // Deliberaly meant to test for one failure
    {
    case 0:
      TEST(i==0); break;
    case 1:
      TEST(i==1); break;
    case 2:
      TEST(false); break;
    case 3:
      TEST(i==3); break;
    case 4:
      TEST(i==4); break;
    default:
      TEST(i==2);
    }
  }

  ENDTEST;
}*/

//void destroynode(std::pair<int,int>* data)
//{
//  delete data;
//}
//
//struct sp {
//  int x;
//  int y;
//};

/*
int PI_ITERATIONS=500;
double pi=((PI_ITERATIONS<<1)-1)+(PI_ITERATIONS*PI_ITERATIONS);

const char* MBSTESTSTRINGS[] = { "test","test2","test3","test4","test5","test6" };
const wchar_t* WCSTESTSTRINGS[] = { BSS__L("test"),BSS__L("test2"),BSS__L("test3"),BSS__L("test4"),BSS__L("test5"),BSS__L("test6") };

struct weird
{
  void* p1;
  __int64 i;
  short offset;
  char blah;
  inline bool valid() { return i==-1 && offset == -1 && blah == -1; }
  inline bool invalid() { return i==0 && offset == 0 && blah == 0; }
  inline void invalidate() { i=0; offset=0;blah=0; }
  inline void validate() { i=-1; offset=-1;blah=-1; }
};

void printout(cLinkedList<int,StandardAllocPolicy<cLLNode<int>>,true>& list)
{
  cLLIter<int> cur(list.GetRoot());

  while(cur.IsValid())
    std::cout<<*(cur++);

  std::cout<<std::endl;
}

void printout(cLinkedArray<int>& list)
{
  auto cur=list.begin();

  while(cur.IsValid())
    std::cout<<*(cur++);

  std::cout<<std::endl;
}*/

//const char* FAKESTRINGLIST[5] = { "FOO", "BAR", "MEH", "SILLY", "EXACERBATION" };

//int main3(int argc, char** argv)
//{  
  //char* romanstuff = inttoroman(3333);

  //cTAATree<int,int> _aatest;

  //for(int i = 0; i < 100000; ++i)
  //{
  //  _aatest.Insert(rand(),rand());
  //  
  //  const cTAATree<int,int>::TNODE* hold=_aatest.GetFirst();
  //  while(hold)
  //  {
  //    if(hold->next!=0)
  //      assert(hold->key<=hold->next->key);
  //    hold=hold->next;
  //  }
  //}

  //int count=0;
  //const cTAATree<int,int>::TNODE* hold=_aatest.GetFirst();
  //while(hold)
  //{
  //  if(hold->next!=0)
  //    assert(hold->key<=hold->next->key);
  //  hold=hold->next;
  //  ++count;
  //}
  //int prev=0;
  //int cur=1;
  //int res=0;
  //int target=227000;
  //while(cur<=target || !isprime(cur))
  //{
  //  res=cur;
  //  cur+=prev;
  //  prev=res;
  ////}
  
  //std::vector<int> prime_divisors;

  //int check=2;
  //int newnum=cur+1;
  //while(check*check<=newnum)
  //{
  //  if (isprime(check) && newnum % check == 0)           
  //  {
  //    prime_divisors.push_back(check);
  //  }
  //  ++check;
  //}
  //
  //int sum=0;
  //for(int i = 0; i < prime_divisors.size(); ++i) sum+=prime_divisors[i];

//	return 0;
//}

 int rometoint(std::string roman) {
    int total = 0;
    int ascii = 0;
    int* cache = new int[roman.length()];

    for (uint i = 0; i < roman.length(); i++) {
        ascii = int(toupper(roman[i]));

        switch (ascii) {
            case 73:
                cache[i] = 1;
                break;
            case 86:
                cache[i] = 5;
                break;
            case 88:
                cache[i] = 10;
                break;
            case 76:
                cache[i] = 50;
                break;
            case 67:
                cache[i] = 100;
                break;
            case 68:
                cache[i] = 500;
                break;
            case 77:
                cache[i] = 1000;
                break;
            default:
                cache [i] = 0;
                break;
        }
    }

    if (roman.length() == 1) {
        return (cache[0]);
    }

    for (uint i = 0; i < (roman.length() - 1); i++) {
        if (cache[i] >= cache[i + 1]) {
            total += cache[i];
        } else {
            total -= cache[i];
        }
    }

    total += cache[roman.length() - 1];
    delete [] cache;
    return (total);
}

//inline bool backwardscheck(const char* begin, int length)
//{
//  int mid = length/2;
//  --length;
//  for(int i = 0; i <= mid; ++i)
//    if(begin[i]!=begin[length-i]) return false;
//
//  return true;
//}

// This is painfully slow and I don't even know why its here.
inline bool isprime(int number)
{
  if(number%2==0) return number==2;
  int stop=number/2;
  for(int i = 3; i < stop; ++i)
    if(number%i==0) return false;
  return true;
}

//inline int addrecursive(int start,int prev)
//{
//  int retval=numarray[start]==prev?1:0;
//}


  //double x=0; // initial position
  //double t=0;
  //unsigned int steps=100;
  //double step=5.0/steps;
  //for(unsigned int i = 0; i < steps; ++i)
  //{
  //  double fx = 1 - x*x;
  //  //x = x + fx*step;
  //  double xt = x + fx*step;
  //  double fxt = 1 - xt*xt;
  //  x = x + 0.5*(fx + fxt)*step;
  //  if(i%10==9) 
  //    std::cout << x << std::endl;
  //}
  //std::cin.get();
  //return 0;

extern void kdtestmain();

char* inttoroman(int in)
{
	int charnum=0;
	int i2=0;
	int hold=in;
	
	for(int j=1000; j>0; j/=5)
	{
		charnum+=(i2=(hold/j));
		hold-=i2*j;
		j/=2;
    if(!j) break;
		charnum+=(i2=(hold/j));
		hold-=i2*j;
	}
	
	char* str=new char[charnum+1];
	for(int i=0; i<charnum+1;++i) str[i]=0;
	
	int count=-1;
	while(in>=1000) { str[++count]='M'; in-=1000; };
	while(in>=900) { str[++count]='C'; str[++count]='M'; in-=900; };
	while(in>=500) { str[++count]='D'; in-=500; };
	while(in>=400) { str[++count]='C'; str[++count]='D'; in-=400; };
	while(in>=100) { str[++count]='C'; in-=100; };
	while(in>=90) { str[++count]='X'; str[++count]='C'; in-=90; };
	while(in>=50) { str[++count]='L'; in-=50; };
	while(in>=40) { str[++count]='X'; str[++count]='L'; in-=40; };
	while(in>=10) { str[++count]='X'; in-=10; };
	while(in>=9) { str[++count]='I'; str[++count]='X'; in-=9; };
	while(in>=5) { str[++count]='V'; in-=5; };
	while(in>=4) { str[++count]='I'; str[++count]='V'; in-=4; };
	while(in>=1) { str[++count]='I'; in-=1; };
	
	return str;
}

