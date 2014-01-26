// Copyright �2014 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#include "cINIstorage.h"
#include <memory.h>

using namespace bss_util;

cINIentry cINIsection::_entrysentinel;
cLocklessFixedAlloc<cINIsection::_NODE> cINIsection::_alloc;

cINIsection::cINIsection(const cINIsection& copy) : _name(copy._name),_parent(copy._parent),_index(copy._index),_root(0),_last(0)
{
  _copy(copy);
}
cINIsection::cINIsection(cINIsection&& mov) : _name(std::move(mov._name)),_parent(mov._parent),_index(mov._index),
  _entries(std::move(mov._entries)),_root(mov._root),_last(mov._last)
{
  mov._root=0;
  mov._last=0;
}
cINIsection::cINIsection() : _parent(0), _index((unsigned int)-1), _root(0), _last(0)
{
}
cINIsection::cINIsection(const char* name, cINIstorage* parent, unsigned int index) : _name(name), _parent(parent), _index(index), _root(0),_last(0)
{
}
cINIsection::~cINIsection()
{
  _destroy();
}
void cINIsection::_destroy()
{
  _NODE* t;
  while(_root)
  {
    t=_root->next;
    _root->~_NODE();
    _alloc.dealloc(_root);
    _root=t;
  }
  _last=0;
}
void cINIsection::_copy(const cINIsection& copy)
{
  assert(!_root && !_last);
  _NODE* t=copy._root;
  _NODE* last=0;
  unsigned int c=0;
  while(t)
  {
    _NODE* p=_alloc.alloc(1);
    memset(p,0,sizeof(_NODE));
    new (&p->val) cINIentry(t->val);
    if(!_root)
      _root=_last=p;
    else
      _last=LLAddAfter(p,_last);

    if(t->instances.Size()!=0) {
      _entries.Insert(p->val.GetKey(),p);
      p->instances.SetSize(c=t->instances.Size());
      last=t;
      --c;
    } else if(c>0) {
      assert(last!=0);
      last->instances[last->instances.Size()-(c--)-1]=p; //This never goes negative because c>0 and is therefore at least 1
    } else
      _entries.Insert(p->val.GetKey(),p);

    t=t->next;
  }
}

void cINIsection::_addentry(const char* key, const char* data)
{
  _NODE* p=_alloc.alloc(1);
  memset(p,0,sizeof(_NODE));
  new (&p->val) cINIentry(key,data);
  khiter_t iter=_entries.Iterator(key);

  if(iter==_entries.End())
  {
    _entries.Insert(p->val.GetKey(),p);
    if(!_root)
      _root=_last=p;
    else
      _last=LLAddAfter(p,_last);
  } else {
    assert(_last!=0 && _root!=0);
    _NODE* r=_entries.UnsafeValue(iter);
    _NODE* t=!r->instances.Size()?r:r->instances.Back();
    LLInsertAfter(p,t,_last);
    r->instances.Insert(p,r->instances.Size());
  }
}

cINIsection& cINIsection::operator=(const cINIsection& right)
{ 
  if(&right == this) return *this;
  _name=right._name;
  _index=right._index;
  _parent=right._parent;
  _destroy();
  _entries.Clear();
  _copy(right);
  return *this;
}

cINIsection& cINIsection::operator=(cINIsection&& mov)
{
  if(&mov == this) return *this;
  _name=std::move(mov._name);
  _index=mov._index;
  _parent=mov._parent;
  _entries=std::move(mov._entries);
  _root=mov._root;
  _last=mov._last;
  mov._root=0;
  mov._last=0;
  return *this;
}

cINIsection::_NODE* cINIsection::GetEntryNode(const char* key, unsigned int instance) const
{
  _NODE* n = _entries[key];
  if(!n) return 0;
  if(!instance) return n;
  return (instance>n->instances.Size())?0:(n->instances[instance-1]);
}

cINIentry* cINIsection::GetEntryPtr(const char* key, unsigned int instance) const
{ 
  _NODE* entry=GetEntryNode(key,instance);
  return !entry?0:&entry->val;
}

unsigned int cINIsection::GetNumEntries(const char* key) const
{
  _NODE* n = _entries[key];
  if(!n) return 0;
  return n->instances.Size()+1;
}

cINIentry& cINIsection::GetEntry(const char* key, unsigned int instance) const
{ 
  _NODE* entry=GetEntryNode(key,instance);
  return !entry?_entrysentinel:entry->val;
}

char cINIsection::EditEntry(const char* key, const char* data, unsigned int instance)
{  // We put this down here because the compiler never actually inlines it, so there's no point in going through hoops to keep it inline.
  return !_parent?-1:_parent->EditEntry(_name,key,data,instance,_index); 
}
