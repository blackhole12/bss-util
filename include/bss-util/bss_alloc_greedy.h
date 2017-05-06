// Copyright �2017 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#ifndef __BSS_ALLOC_GREEDY_H__
#define __BSS_ALLOC_GREEDY_H__

#include <atomic>
#include "bss-util/bss_alloc.h"
#include "bss-util/bss_util.h"
#include "rwlock.h"

namespace bss {
  struct AFLISTITEM
  {
    AFLISTITEM* next;
    size_t size;
  };

  // Lockless dynamic greedy allocator that can allocate any number of bytes
  class BSS_COMPILER_DLLEXPORT GreedyAlloc
  {
    GreedyAlloc(const GreedyAlloc& copy) BSS_DELETEFUNC
      GreedyAlloc& operator=(const GreedyAlloc& copy) BSS_DELETEFUNCOP

  public:
    inline GreedyAlloc(GreedyAlloc&& mov) : _root(mov._root.load(std::memory_order_relaxed)), _curpos(mov._curpos.load(std::memory_order_relaxed)) { mov._root.store(nullptr, std::memory_order_relaxed); mov._curpos = 0; }
    inline explicit GreedyAlloc(size_t init = 64) : _root(0), _curpos(0)
    {
      _allocChunk(init);
    }
    inline ~GreedyAlloc()
    {
      _lock.Lock();
      AFLISTITEM* hold = _root.load(std::memory_order_relaxed);
      while((_root = hold))
      {
        hold = hold->next;
        free(_root.load(std::memory_order_relaxed));
      }
    }
    template<typename T>
    inline T* allocT(size_t num) noexcept
    {
      return (T*)alloc(num * sizeof(T));
    }
    inline void* alloc(size_t _sz) noexcept
    {
#ifdef BSS_DISABLE_CUSTOM_ALLOCATORS
      return malloc(_sz);
#endif
      size_t r;
      AFLISTITEM* root;
      for(;;)
      {
        _lock.RLock();
        r = _curpos.fetch_add(_sz, std::memory_order_relaxed);
        size_t rend = r + _sz;
        root = _root.load(std::memory_order_relaxed);
        if(rend >= root->size)
        {
          if(_lock.AttemptUpgrade())
          {
            if(rend >= root->size) // We do another check in here to ensure another thread didn't already resize the root for us.
            {
              _allocChunk(fbnext(rend));
              _curpos.store(0, std::memory_order_relaxed);
            }
            _lock.Downgrade();
          }
          _lock.RUnlock();
        }
        else
          break;
      }

      void* retval = ((char*)(root + 1)) + r;
      _lock.RUnlock();
      return retval;
    }
    void dealloc(void* p) noexcept
    {
#ifdef BSS_DISABLE_CUSTOM_ALLOCATORS
      delete p; return;
#endif
#ifdef BSS_DEBUG
      _lock.RLock();
      AFLISTITEM* cur = _root.load(std::memory_order_relaxed);
      bool found = false;
      while(cur)
      {
        if(p >= (cur + 1) && p < (((char*)(cur + 1)) + cur->size)) { found = true; break; }
        cur = cur->next;
      }
      _lock.RUnlock();
      assert(found);
      //memset(p,0xFEEEFEEE,sizeof(T)); //No way to know how big this is
#endif
    }
    void Clear() noexcept
    {
      _lock.Lock();
      AFLISTITEM* root = _root.load(std::memory_order_acquire);
      if(!root->next) return _lock.Unlock();
      size_t nsize = 0;
      AFLISTITEM* hold;
      while(root)
      {
        hold = root->next;
        nsize += root->size;
        free(root);
        root = hold;
      }
      _root.store(nullptr, std::memory_order_release);
      _allocChunk(nsize); //consolidates all memory into one chunk to try and take advantage of data locality
      _curpos.store(0, std::memory_order_relaxed);
      _lock.Unlock();
    }
  protected:
    BSS_FORCEINLINE void _allocChunk(size_t nsize) noexcept
    {
      AFLISTITEM* retval = reinterpret_cast<AFLISTITEM*>(malloc(sizeof(AFLISTITEM) + nsize));
      retval->next = _root.load(std::memory_order_acquire);
      retval->size = nsize;
      assert(_prepDEBUG(retval));
      _root.store(retval, std::memory_order_release);
    }
#ifdef BSS_DEBUG
    BSS_FORCEINLINE static bool _prepDEBUG(AFLISTITEM* root) noexcept
    {
      if(!root) return false;
      memset(root + 1, 0xfd, root->size);
      return true;
    }
#endif

#pragma warning(push)
#pragma warning(disable:4251)
    std::atomic<AFLISTITEM*> _root;
    std::atomic<size_t> _curpos;
#pragma warning(pop)
    RWLock _lock;
  };

  template<typename T>
  class BSS_COMPILER_DLLEXPORT GreedyPolicy : protected GreedyAlloc {
  public:
    typedef T* pointer;
    typedef T value_type;
    template<typename U>
    struct rebind { typedef GreedyPolicy<U> other; };

    inline GreedyPolicy() {}
    inline ~GreedyPolicy() {}

    inline pointer allocate(std::size_t cnt, const pointer = 0) noexcept { return GreedyAlloc::allocT<T>(cnt); }
    inline void deallocate(pointer p, std::size_t num = 0) noexcept {}
    inline void clear() noexcept { GreedyAlloc::Clear(); } //done for functor reasons, has no effect here
  };
}


#endif