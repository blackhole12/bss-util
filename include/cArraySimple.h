// Copyright ©2012 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#ifndef __C_ARRAY_SIMPLE_H__BSS__
#define __C_ARRAY_SIMPLE_H__BSS__

#include "bss_call.h"
#include <memory.h>
#include <malloc.h>

namespace bss_util {
  // Very simple "dynamic" array. Designed to be used when size must be maintained at an exact value.
  template<class T, typename SizeType=unsigned int>
  class BSS_COMPILER_DLLEXPORT cArraySimple
  {
  public:
    inline cArraySimple<T,SizeType>(const cArraySimple<T,SizeType>& copy) : _array(!copy._size?(T*)0:(T*)malloc(copy._size*sizeof(T))), _size(copy._size)
    {
      memcpy(_array,copy._array,_size*sizeof(T));
    }
    inline cArraySimple<T,SizeType>(cArraySimple<T,SizeType>&& mov) : _array(mov._array), _size(mov._size)
    {
      mov._array=0;
      mov._size=0;
    }
    inline explicit cArraySimple<T,SizeType>(SizeType size) : _array(!size?(T*)0:(T*)malloc(size*sizeof(T))), _size(size)
    {
    }
    inline ~cArraySimple<T,SizeType>()
    {
      if(_array!=0)
        free(_array);
    }
    inline SizeType Size() const { return _size; }
    inline void BSS_FASTCALL SetSizeDiscard(SizeType nsize)
    {
      if(nsize==_size) return;
      if(_array!=0) free(_array);
      _array = (T*)_minmalloc(nsize*sizeof(T));
      _size=nsize;
    }
    inline void BSS_FASTCALL SetSize(SizeType nsize)
    {
      if(nsize==_size) return;
      T* narray = (T*)_minmalloc(nsize*sizeof(T));
      memcpy(narray,_array,((nsize<_size)?(nsize):(_size))*sizeof(T));
      if(_array!=0) free(_array);
      _array=narray;
      _size=nsize;
    }
    inline void BSS_FASTCALL RemoveInternal(SizeType index)
    {
      memmove(_array+index,_array+index+1,(_size-index-1)*sizeof(T));
      //--_size;
    }
    inline void BSS_FASTCALL Insert(T item, SizeType location)
    {
      SizeType nsize=_size+1;
      T* narray = (T*)_minmalloc(nsize*sizeof(T));
      memcpy(narray,_array,location*sizeof(T));
      narray[location]=item;
      memcpy(narray+location+1,_array+location,(_size-location)*sizeof(T));
      if(_array!=0) free(_array);
      _array=narray;
      _size=nsize;
    }
    //inline operator T*() { return _array; }
    //inline operator const T*() const { return _array; }
    inline cArraySimple<T,SizeType>& operator=(const cArraySimple<T,SizeType>& copy)
    {
      if(this == &copy) return *this;
      if(_array!=0) free(_array);
      _size=copy._size;
      _array=(T*)_minmalloc(_size*sizeof(T));
      memcpy(_array,copy._array,_size*sizeof(T));
      return *this;
    }
    inline cArraySimple<T,SizeType>& operator=(cArraySimple<T,SizeType>&& mov)
    {
      if(this == &mov) return *this;
      if(_array!=0) free(_array);
      _array=mov._array;
      _size=mov._size;
      mov._array=0;
      mov._size=0;
      return *this;
    }
    inline cArraySimple<T,SizeType>& operator +=(const cArraySimple<T,SizeType>& add)
    {
      SizeType oldsize=_size;
      SetSize(_size+add._size);
      memcpy(_array+oldsize,add._array,add._size*sizeof(T));
      return *this;
    }
    inline const cArraySimple<T,SizeType> operator +(const cArraySimple<T,SizeType>& add)
    {
      cArraySimple<T,SizeType> retval(*this);
      retval+=add;
      return retval;
    }
    inline void Scrub(int val)
    {
      memset(_array,val,_size*sizeof(T));
    }

  protected:
    inline BSS_FORCEINLINE static void* _minmalloc(size_t n) { return malloc((n<1)?1:n); } //Malloc can legally return NULL if it tries to allocate 0 bytes
    template<typename U>
    inline void _pushback(SizeType index, SizeType length, U && data) 
    {
      _mvarray(index+1,index,length);
      _array[index]=std::forward<U>(data);
    }
    inline void _mvarray(SizeType begin, SizeType end, SizeType length)
    {
      memmove(_array+begin,_array+end,length*sizeof(T));
    }
    //inline void _setsize(SizeType nsize, int val)
    //{
    //  SizeType last=_size;
    //  SetSize(nsize);
    //  if(_size>last)
    //    memset(_array+last,val,(_size-last)*sizeof(T));
    //}

    T* _array;
    SizeType _size;

    typedef SizeType ST_;
    typedef T T_;
  };

  // Very simple "dynamic" array that calls the constructor and destructor
  template<class T, typename SizeType=unsigned int>
  class BSS_COMPILER_DLLEXPORT cArrayConstruct
  {
  public:
    inline cArrayConstruct(const cArrayConstruct& copy) : _array((T*)_minmalloc(copy._size*sizeof(T))), _size(copy._size)
    {
      //memcpy(_array,copy._array,_size*sizeof(T)); // Can't use memcpy on an external source because you could end up copying a pointer that would later be destroyed
      for(SizeType i = 0; i < _size; ++i)
        new (_array+i) T(copy._array[i]);
    }
    inline cArrayConstruct(cArrayConstruct&& mov) : _array(mov._array), _size(mov._size)
    {
      mov._array=0;
      mov._size=0;
    }
    inline explicit cArrayConstruct(SizeType size) : _array((T*)_minmalloc(size*sizeof(T))), _size(size)
    {
      for(SizeType i = 0; i < _size; ++i)
        new (_array+i) T();
    }
    inline ~cArrayConstruct()
    {
      for(SizeType i = 0; i < _size; ++i)
        (_array+i)->~T();
      if(_array!=0)
        free(_array);
    }
    inline SizeType Size() const { return _size; }
    inline void BSS_FASTCALL SetSize(SizeType nsize)
    {
      if(nsize==_size) return;
      T* narray = (T*)_minmalloc(nsize*sizeof(T));
      memcpy(narray,_array,((nsize<_size)?(nsize):(_size))*sizeof(T)); // We can do this because these aren't external sources.

      if(nsize<_size) { //we removed some so we need to destroy them
        for(SizeType i = _size; i > nsize;)
          (_array+(--i))->~T();
      } else { //we created some so we need to construct them
        for(SizeType i = _size; i < nsize; ++i)
          new(narray+i) T();
      }

      if(_array!=0) free(_array);
      _array=narray;
      _size=nsize;
    }
    inline void BSS_FASTCALL RemoveInternal(SizeType index)
    {
      _array[index].~T();
      memmove(_array+index,_array+index+1,(_size-index-1)*sizeof(T));
      new(_array+(_size-1)) T();
    }
    //inline operator T*() { return _array; }
    //inline operator const T*() const { return _array; }
    inline cArrayConstruct<T,SizeType>& operator=(const cArrayConstruct<T,SizeType>& copy)
    {
      if(this == &copy) return *this;
      for(SizeType i = 0; i < _size; ++i)
        (_array+i)->~T();
      if(_array!=0) free(_array);
      _size=copy._size;
      _array=(T*)_minmalloc(_size*sizeof(T));
      //memcpy(_array,copy._array,_size*sizeof(T));
      for(SizeType i = 0; i < _size; ++i)
        new (_array+i) T(copy._array[i]);
      return *this;
    }
    inline cArrayConstruct<T,SizeType>& operator=(cArrayConstruct<T,SizeType>&& mov)
    {
      if(this == &mov) return *this;
      for(SizeType i = 0; i < _size; ++i)
        (_array+i)->~T();
      if(_array!=0) free(_array);
      _array=mov._array;
      _size=mov._size;
      mov._array=0;
      mov._size=0;
      return *this;
    }
    inline cArrayConstruct<T,SizeType>& operator +=(const cArrayConstruct<T,SizeType>& add)
    {
      SizeType nsize=_size+add._size;
      T* narray = (T*)_minmalloc(nsize*sizeof(T));
      memcpy(narray,_array,_size*sizeof(T));
      //memcpy(narray+_size,add._array,add._size*sizeof(T));
      if(_array!=0) free(_array);
      _array=narray;
      
      for(SizeType i = _size; i < nsize; ++i)
        new (_array+i) T(add._array[i-_size]);

      _size=nsize;
      return *this;
    }
    inline const cArrayConstruct<T,SizeType> operator +(const cArrayConstruct<T,SizeType>& add)
    {
      cArrayConstruct<T,SizeType> retval(*this);
      retval+=add;
      return retval;
    }

  protected:
    inline BSS_FORCEINLINE static void* _minmalloc(size_t n) { return malloc((n<1)?1:n); } //Malloc can legally return NULL if it tries to allocate 0 bytes
    template<typename U>
    inline void _pushback(SizeType index, SizeType length, U && data) 
    {
      (_array+(index+length))->~T();
      memmove(_array+(index+1),_array+index,length*sizeof(T));
      new (_array+index) T(std::forward<U>(data));
    }
    inline void _mvarray(SizeType begin, SizeType end, SizeType length)
    {
      memmove(_array+begin,_array+end,length*sizeof(T));
    }

    T* _array;
    SizeType _size;
    
    typedef SizeType ST_;
    typedef T T_;
  };

  // Typesafe array that reconstructs everything properly, without any memory moving tricks
  template<class T, typename SizeType=unsigned int>
  class BSS_COMPILER_DLLEXPORT cArraySafe
  {
  public:
    inline cArraySafe(const cArraySafe& copy) : _array((T*)_minmalloc(copy._size*sizeof(T))), _size(copy._size)
    {
      for(SizeType i = 0; i < _size; ++i)
        new (_array+i) T(copy._array[i]);
    }
    inline cArraySafe(cArraySafe&& mov) : _array(mov._array), _size(mov._size)
    {
      mov._array=0;
      mov._size=0;
    }
    inline explicit cArraySafe(SizeType size) : _array((T*)_minmalloc(size*sizeof(T))), _size(size)
    {
      for(SizeType i = 0; i < _size; ++i)
        new (_array+i) T();
    }
    inline ~cArraySafe()
    {
      for(SizeType i = 0; i < _size; ++i)
        (_array+i)->~T();
      if(_array!=0)
        free(_array);
    }
    inline SizeType Size() const { return _size; }
    inline void BSS_FASTCALL SetSize(SizeType nsize)
    {
      if(nsize==_size) return;
      T* narray = (T*)_minmalloc(nsize*sizeof(T));
      
      SizeType smax = _size<nsize?_size:nsize;
      for(SizeType i = 0; i < smax; ++i) //copy over any we aren't discarding
        new (narray+i) T(std::move(_array[i])); //We're going to be deleting the old ones so use move semantics if possible
      for(SizeType i = smax; i < nsize; ++i) //Initialize any newcomers
        new (narray+i) T();
      for(SizeType i = 0; i < _size; ++i) //Demolish the old ones
        (_array+i)->~T();

      if(_array!=0) free(_array);
      _array=narray;
      _size=nsize;
    }
    inline void BSS_FASTCALL RemoveInternal(SizeType index)
    {
      --_size; // Note that this _size decrease is reversed at the end of this function, so _size doesn't actually change, matching the behavior of cArraySimple/cArraySafe
      for(SizeType i=index; i<_size;++i)
        _array[i]=std::move(_array[i+1]);
      _array[_size].~T();
      new(_array+(_size++)) T();
    }
    //inline operator T*() { return _array; }
    //inline operator const T*() const { return _array; }
    inline cArraySafe<T,SizeType>& operator=(const cArraySafe<T,SizeType>& copy)
    {
      if(this == &copy) return *this;
      for(SizeType i = 0; i < _size; ++i)
        (_array+i)->~T();
      if(_array!=0) free(_array);
      _size=copy._size;
      _array=(T*)_minmalloc(_size*sizeof(T));
      for(SizeType i = 0; i < _size; ++i)
        new (_array+i) T(copy._array[i]);
      return *this;
    }
    inline cArraySafe<T,SizeType>& operator=(cArraySafe<T,SizeType>&& mov)
    {
      if(this == &mov) return *this;
      for(SizeType i = 0; i < _size; ++i)
        (_array+i)->~T();
      if(_array!=0) free(_array);
      _array=mov._array;
      _size=mov._size;
      mov._array=0;
      mov._size=0;
      return *this;
    }
    inline cArraySafe<T,SizeType>& operator +=(const cArraySafe<T,SizeType>& add)
    {
      SizeType nsize=_size+add._size;
      T* narray = (T*)_minmalloc(nsize*sizeof(T));
      
      for(SizeType i = 0; i < _size; ++i) //copy over old ones
        new (narray+i) T(std::move(_array[i])); //We're going to delete the old ones so use move semantics if possible
      for(SizeType i = _size; i < nsize; ++i) //Copy over newcomers
        new (_array+i) T(add._array[i-_size]);
      for(SizeType i = 0; i < _size; ++i) //Demolish the old ones
        (_array+i)->~T();

      if(_array!=0) free(_array);
      _array=narray;
      _size=nsize;
      return *this;
    }
    inline const cArraySafe<T,SizeType> operator +(const cArraySafe<T,SizeType>& add)
    {
      cArrayConstruct<T,SizeType> retval(*this);
      retval+=add;
      return retval;
    }

  protected:
    inline BSS_FORCEINLINE static void* _minmalloc(size_t n) { return malloc((n<1)?1:n); }  //Malloc can legally return NULL if it tries to allocate 0 bytes
    template<typename U>
    inline void _pushback(SizeType index, SizeType length, U && data) 
    {
      for(SizeType i=index+length; i>index; --i)
        _array[i]=_array[i-1];
      _array[index] = std::forward<U>(data);
    }
    inline void _mvarray(SizeType begin, SizeType end, SizeType length)
    {
      if(begin>end)
      {
        for(SizeType i=0; i<length;++i)
          _array[end+i]=std::move(_array[begin+i]);
      }
      else
      {
        for(SizeType i=length; i-->0;)
          _array[end+i]=std::move(_array[begin+i]);
      }
    }

    T* _array;
    SizeType _size;
    
    typedef SizeType ST_;
    typedef T T_;
  };
  
  // Wrapper for underlying arrays that expose the array, making them independently usable without blowing up everything that inherits them
  template<class ARRAYTYPE>
  class BSS_COMPILER_DLLEXPORT cArrayWrap : public ARRAYTYPE
  {
  protected:
    typedef typename ARRAYTYPE::ST_ ST_;
    typedef typename ARRAYTYPE::T_ T_;
    typedef ARRAYTYPE AT_;

  public:
    //inline cArrayWrap(const cArrayWrap& copy) : AT_(copy) {}
    inline cArrayWrap(cArrayWrap&& mov) : AT_(std::move(mov)) {}
    inline explicit cArrayWrap(ST_ size=0): AT_(size) {}
    
    //inline void Add(T item) { AT_::Insert(item,_size); } // Not all cArrays implement Insert
    //Implementation of RemoveInternal that adjusts the size of the array.
    inline void Remove(ST_ index) { AT_::RemoveInternal(index); AT_::SetSize(AT_::_size-1); }
    inline const T_& Front() const { assert(AT_::_size>0); return AT_::_array[0]; }
    inline T_& Front() { assert(AT_::_size>0); return AT_::_array[0]; }
    inline const T_& Back() const { assert(AT_::_size>0); return AT_::_array[AT_::_size-1]; }
    inline T_& Back() { assert(AT_::_size>0); return AT_::_array[AT_::_size-1]; }
    inline operator T_*() { return AT_::_array; }
    inline operator const T_*() const { return AT_::_array; }
    inline const T_* begin() const { return AT_::_array; }
    inline const T_* end() const { return AT_::_array+AT_::_size; }
    inline T_* begin() { return AT_::_array; }
    inline T_* end() { return AT_::_array+AT_::_size; }

    //inline cArrayWrap& operator=(const AT_& copy) { AT_::operator=(copy); return *this; }
    inline cArrayWrap& operator=(AT_&& mov) { AT_::operator=(std::move(mov)); return *this; }
    inline cArrayWrap& operator +=(const AT_& add) { AT_::operator+=(add); return *this; }
    inline const cArrayWrap operator +(const AT_& add) { cArrayWrap r(*this); return (r+=add); }
  };
  
  // Templatized typedefs for making this easier to use
  template<class T, typename SizeType=unsigned int>
  struct BSS_COMPILER_DLLEXPORT DArray
  {
    typedef cArrayWrap<cArraySimple<T,SizeType>> t;
    typedef cArrayWrap<cArrayConstruct<T,SizeType>> tConstruct;
    typedef cArrayWrap<cArraySafe<T,SizeType>> tSafe;
  };
}

#endif