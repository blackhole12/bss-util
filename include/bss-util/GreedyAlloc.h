// Copyright �2017 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#ifndef __BSS_ALLOC_GREEDY_H__
#define __BSS_ALLOC_GREEDY_H__

#include <atomic>
#include "Alloc.h"
#include "bss_util.h"
#include "RWLock.h"

namespace bss {
  // Lockless dynamic greedy allocator that can allocate any number of bytes
  class BSS_COMPILER_DLLEXPORT GreedyAlloc
  {
    GreedyAlloc(const GreedyAlloc& copy) = delete;
    GreedyAlloc& operator=(const GreedyAlloc& copy) = delete;

    struct Node
    {
      Node* next;
      size_t size;
    };

  public:
    inline GreedyAlloc(GreedyAlloc&& mov) : _root(mov._root.load(std::memory_order_relaxed)), _curpos(mov._curpos.load(std::memory_order_relaxed)), 
      _alignsize(mov._alignsize), _align(mov._align)
    { 
      mov._root.store(nullptr, std::memory_order_relaxed);
      mov._curpos = 0; 
    }
    inline explicit GreedyAlloc(size_t init = 64, size_t align = 1) : _root(0), _curpos(0), _align(align),
      _alignsize(AlignSize(sizeof(Node), align))
    {
      _allocChunk(init);
    }
    inline ~GreedyAlloc()
    {
      _lock.Lock();
      Node* hold = _root.load(std::memory_order_relaxed);

      while((_root = hold))
      {
        hold = hold->next;
        ALIGNEDFREE(_root.load(std::memory_order_relaxed));
      }
    }
    template<typename T>
    inline T* AllocT(size_t num) noexcept
    {
      return (T*)Alloc(num * sizeof(T));
    }
    inline void* Alloc(size_t sz) noexcept
    {
      sz = AlignSize(sz, _align);
#ifdef BSS_DISABLE_CUSTOM_ALLOCATORS
      return ALIGNEDALLOC(sz, _align);
#endif
      size_t r;
      Node* root;

      for(;;)
      {
        _lock.RLock();
        r = _curpos.fetch_add(sz, std::memory_order_relaxed);
        size_t rend = r + sz;
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
      
      void* retval = reinterpret_cast<uint8_t*>(root) + _alignsize + r;
      _lock.RUnlock();
      return retval;
    }
    void Dealloc(void* p) noexcept
    {
#ifdef BSS_DISABLE_CUSTOM_ALLOCATORS
      ALIGNEDFREE(p); return;
#endif
#ifdef BSS_DEBUG
      _lock.RLock();
      Node* cur = _root.load(std::memory_order_relaxed);
      bool found = false;

      while(cur)
      {
        if(p >= (reinterpret_cast<uint8_t*>(cur) + _alignsize) && p < (reinterpret_cast<uint8_t*>(cur) + _alignsize + cur->size)) { found = true; break; }
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
      Node* root = _root.load(std::memory_order_acquire);
      _curpos.store(0, std::memory_order_relaxed);

      if(!root->next)
        return _lock.Unlock();

      size_t nsize = 0;
      Node* hold;

      while(root)
      {
        hold = root->next;
        nsize += root->size;
        ALIGNEDFREE(root);
        root = hold;
      }

      _root.store(nullptr, std::memory_order_release);
      _allocChunk(nsize); //consolidates all memory into one chunk to try and take advantage of data locality
      _lock.Unlock();
    }
  protected:
    BSS_FORCEINLINE void _allocChunk(size_t nsize) noexcept
    {
      Node* retval = reinterpret_cast<Node*>(ALIGNEDALLOC(_alignsize + nsize, _align));
      assert(retval != 0);
      retval->next = _root.load(std::memory_order_acquire);
      retval->size = nsize;
      assert(_prepDEBUG(retval, _alignsize));
      _root.store(retval, std::memory_order_release);
    }
#ifdef BSS_DEBUG
    BSS_FORCEINLINE static bool _prepDEBUG(Node* root, size_t alignsize) noexcept
    {
      if(!root) return false;
      memset(reinterpret_cast<uint8_t*>(root) + alignsize, 0xfd, root->size);
      return true;
    }
#endif

#pragma warning(push)
#pragma warning(disable:4251)
    std::atomic<Node*> _root;
    std::atomic<size_t> _curpos;
#pragma warning(pop)
    RWLock _lock;
    const size_t _align;
    const size_t _alignsize;
  };

  template<typename T>
  class BSS_COMPILER_DLLEXPORT GreedyPolicy : protected GreedyAlloc {
  public:
    typedef T* pointer;
    typedef T value_type;
    template<typename U>
    struct rebind { typedef GreedyPolicy<U> other; };

    inline GreedyPolicy(size_t init = 8) : GreedyAlloc(init * sizeof(T), alignof(T)) {}
    inline ~GreedyPolicy() {}

    inline pointer allocate(std::size_t cnt, const pointer = 0) noexcept { return GreedyAlloc::AllocT<T>(cnt); }
    inline void deallocate(pointer p, std::size_t num = 0) noexcept { GreedyAlloc::Dealloc(p); }
    inline void clear() noexcept { GreedyAlloc::Clear(); } //done for functor reasons, has no effect here
  };
}


#endif