// Copyright �2012 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in "bss_util.h"

#include "cINIstorage.h"
#include <sstream>
#include <iomanip>
#include <stdio.h>
#include <string.h>
#include "bss_deprecated.h"
#include "bss_util_c.h"
#include "cStr.h"
#include "INIparse.h"

using namespace bss_util;

template<typename T>
const T* BSS_FASTCALL _trimlstr(const T* str)
{
  for(;*str>0 && *str<33;++str);
  return str;
}

template<typename T>
const T* BSS_FASTCALL _trimrstr(const T* end,const T* begin)
{
  for(;end>begin && *end<33;--end);
  return end;
}

void cINIstorage::_openINI()
{
  cStr targetpath=_path;
  targetpath+=_filename;
  FILE* f;
  FOPEN(f,targetpath,"a+b"); //this will create the file if it doesn't already exist
  if(!f) return;
  fseek(f,0,SEEK_END);
  size_t size=(size_t)ftell(f);
  fseek(f,0,SEEK_SET);
  _ini = new cStr(size+1);
  size=fread(_ini->UnsafeString(),sizeof(char),size,f); //reads in the entire file
  _ini->UnsafeString()[size]='\0';
  _ini->RecalcSize();
  fclose(f);
}

cINIsection cINIstorage::_sectionsentinel;
cFixedAlloc<cINIstorage::_NODE,4> cINIstorage::_alloc;

cINIstorage::cINIstorage(const cINIstorage& copy) : _path(copy._path), _filename(copy._filename),
  _ini(!copy._ini?0:new cStr(*copy._ini)), _logger(copy._logger), _root(0), _last(0)
{
  _copyhash(copy);
}

cINIstorage::cINIstorage(cINIstorage&& mov) : _path(std::move(mov._path)), _filename(std::move(mov._filename)), _ini(mov._ini),
  _logger(mov._logger), _sections(std::move(mov._sections)), _root(mov._root), _last(mov._last)
{
  mov._ini=0;
  mov._root=0;
  mov._last=0;
}
cINIstorage::cINIstorage(const char* file, std::ostream* logger) : _ini(0), _logger(logger), _root(0), _last(0)
{
  FILE* f;
  FOPEN(f,file,("rt"));
  if(f)
  {
    _loadINI(f);
    fclose(f);
  }
  _setfilepath(file);
}
cINIstorage::cINIstorage(FILE* file, std::ostream* logger) : _ini(0), _logger(logger), _root(0), _last(0)
{
  _loadINI(file);
}

// Destructor
cINIstorage::~cINIstorage()
{
  if(_ini) delete _ini;
  _destroyhash(); //_destroyhash checks for nullification
}
cINIsection* cINIstorage::GetSection(const char* section, unsigned int instance) const
{
  khiter_t iter= _sections.GetIterator(section);
  if(iter==_sections.End()) return 0;
  __ARR* arr=_sections[iter];
  return instance<arr->Size()?(*arr)+instance:0;
}
cINIentry* cINIstorage::GetEntryPtr(const char *section, const char* key, unsigned int keyinstance, unsigned int instance) const
{
  khiter_t iter= _sections.GetIterator(section);
  if(iter==_sections.End()) return 0;
  __ARR* arr=_sections[iter];
  return instance<arr->Size()?(*arr)[instance].GetEntryPtr(key,keyinstance):0;
}

cINIsection& BSS_FASTCALL cINIstorage::AddSection(const char* name)
{
  if(!_ini) _openINI();
  _ini->reserve(_ini->size()+strlen(name)+4);
  char c;
  size_t i;
  for(i = _ini->size(); i > 0;)
  {
    c=_ini->GetChar(--i);
    if(!(c==' ' || c=='\n' || c=='\r')) { ++i; break; }
  }
  if(i>0)
  {
    _ini->UnsafeString()[i]='\0';
    _ini->RecalcSize();
    _ini->append(("\n\n"));
  }
  _ini->append(("["));
  _ini->append(name);
  _ini->append(("]"));
  return *_addsection(name);
}

cINIsection* cINIstorage::_addsection(const char* name)
{
  khiter_t iter=_sections.GetIterator(name);
  __ARR* arr;
  if(iter==_sections.End()) //need to add in an array for this
  {
    arr= new __ARR(1);
    new ((*arr)+0) cINIsection(name,this,0);
    _sections.Insert((*arr)[0].GetName(),arr);
    return ((*arr)+0);
  } else {
    arr=_sections[iter];
    unsigned int index=arr->Size();
    arr->SetSize(index+1);
    new ((*arr)+index) cINIsection(name,this,index);
    _sections.OverrideKeyPtr(iter,(*arr)[0].GetName()); //the key pointer gets corrupted when we resize the array
    return ((*arr)+index);
  }
}

//3 cases: removing something other then the beginning, removing the beginning, or removing the last one
bool cINIstorage::RemoveSection(const char* name, unsigned int instance)
{
  if(!_ini) _openINI();
  __ARR* arr;
  INICHUNK chunk = bss_findINIsection(*_ini,_ini->size(),name,instance);
  khiter_t iter = _sections.GetIterator(name);
  if(iter!=_sections.End() && chunk.start!=0)
  {
    arr=_sections[iter];
    if(instance>=arr->Size()) return false;
    _ini->erase((const char*)chunk.start-_ini->c_str(),((const char*)chunk.end-(const char*)chunk.start)+1);
    ((*arr)+instance)->~cINIsection();
    if(arr->Size()==1) //this is the last one in the group
    {
      delete arr;
      _sections.RemoveIterator(iter);
    }
    else
    {
      arr->RemoveShrink(instance);
      if(!instance) _sections.OverrideKeyPtr(iter,(*arr)[0].GetName());
      unsigned int svar=arr->Size();
      for(;instance<svar;++instance) --(*arr)[instance]._index; //moves all the indices down
    }

    return true;
  }
  return false;
}

char cINIstorage::EditEntry(const char* section, const char* key, const char* nvalue, unsigned int secinstance, unsigned int keyinstance)
{
  if(!_ini) _openINI();
  if(secinstance==(unsigned int)-1) AddSection(section);

  khiter_t iter = _sections.GetIterator(section);
  if(iter==_sections.End()) return -1; //if it doesn't exist at this point, fail
  __ARR* arr=_sections[iter];
  if(secinstance==(unsigned int)-1) secinstance=arr->Size()-1; //if we just added it, it will be the last entry
  if(secinstance>=arr->Size()) return -2; //if secinstance is not valid, fail
  cINIsection* psec = (*arr)+secinstance;
  INICHUNK chunk = bss_findINIsection(_ini->c_str(),_ini->size(),section,secinstance);
  if(!chunk.start) return -3; //if we can't find it in the INI, fail

  if(keyinstance==(unsigned int)-1) //insertion
  {
    psec->_addentry(key,nvalue);
    //_ini->reserve(_ini->size()+strlen(key)+strlen(nvalue)+2); // This is incredibly stupid. It invalidates all our pointers causing strange horrifying bugs.
    cStr construct(("\n%s=%s"),key,!nvalue?(""):nvalue); //build keyvalue string
    const char* peek=(const char*)chunk.start;
    const char* ins=strchr((const char*)chunk.start,'\n');
    if(!ins) //end of file
    {
      _ini->append(construct);
      return 0;
    }

    _ini->insert(ins-_ini->c_str(),construct);
    return 0;
  } //If it wasn't an insert we need to find the entry before the other two possible cases

  typedef cINIsection::_NODE _SNODE; //This makes things a bit easier

  iter=psec->_entries.GetIterator(key);
  if(iter==psec->_entries.End()) return -4; //if key doesn't exist at this point, fail
  _SNODE* entnode=psec->_entries[iter];
  _SNODE* entroot=entnode;
  if(keyinstance>entnode->instances.Size()) return -5; //if keyinstance is not valid, fail
  if(keyinstance!=0)
    entnode=entnode->instances[keyinstance-1];
  chunk=bss_findINIentry(chunk,key,keyinstance);
  if(!chunk.start) return -7; //if we can't find it

  if(!nvalue) //deletion
  {
    // If you are deleting a root node that has danglers, we just replace the root with one of the danglers and transform it into a dangler case.
    if(!keyinstance && entnode->instances.Size()>0)
    {
      keyinstance=1;
      entnode->val=std::move(entnode->next->val);
      entnode=entnode->next;
    }

    LLRemove(entnode,psec->_root,psec->_last);
    if(!keyinstance) // If this is true you are a root, but you don't have any danglers (see above check), so we just remove you from the hash.
      psec->_entries.RemoveIterator(iter);
    else // Otherwise you are a dangler, so all we have to do is remove you from the instances array
      entroot->instances.RemoveShrink(keyinstance-1);
    
    entnode->~_SNODE(); // Calling this destructor is important in case the node has an unused array that needs to be freed
    cINIsection::_alloc.dealloc(entnode);
    const char* end=strchr((const char*)chunk.end,'\n'); // Now we remove it from the actual INI
    end=!end?(const char*)chunk.end:end+1;
    _ini->erase((const char*)chunk.start-_ini->c_str(),end-(const char*)chunk.start);
  }
  else //edit
  {
    entnode->val.SetData(nvalue);
    const char* start=strchr((const char*)chunk.start,'=');
    if(!start) return -6; //if this happens something is borked
    start=_trimlstr(++start);
    _ini->replace(start-_ini->c_str(),(_trimrstr(((const char*)chunk.end)-1,start)-start)+1,nvalue);
  }

  return 0;
}
void cINIstorage::EndINIEdit(const char* overridepath)
{
  if(!_ini) return;
  if(overridepath!=0) _setfilepath(overridepath);
  cStr targetpath=_path;
  targetpath+=_filename;
  FILE* f;
  FOPEN(f,targetpath,("wb"));
  if(!f) return; // IF the file fails, bail out and do not discard the edit.
  fwrite(*_ini,sizeof(char),_ini->size(),f);
  fclose(f);
  DiscardINIEdit();
}
void cINIstorage::DiscardINIEdit()
{
  if(_ini) delete _ini;
  _ini=0;
}

void cINIstorage::_loadINI(FILE* f)
{
  INIParser parse;
  bss_initINI(&parse,f);
  cINIsection* cursec=0;
  while(bss_parseLine(&parse)!=0)
  {
    if(parse.newsection)
      cursec=_addsection(parse.cursection);
    else
    {
      if(!cursec) cursec=_addsection((""));
      cursec->_addentry(parse.curkey, parse.curvalue);
    }
  }

  //if(parse.newsection) //We don't check for empty sections
  //  cursec=_addsection(parse.cursection); 

  bss_destroyINI(&parse);
}
void cINIstorage::_setfilepath(const char* file)
{
  const char* hold=strrchr(file,'/');
  hold=!hold?file :hold+1;
  const char* hold2=strrchr(hold,'\\');
  hold=!hold2?hold:hold2+1;
  _filename=hold;
  size_t length = hold-file;
  _path.resize(++length);
  memcpy(_path.UnsafeString(),file,length);
  _path.UnsafeString()[length-1]='\0';
  _path.RecalcSize();
}
void cINIstorage::_destroyhash()
{
  if(!_sections.Nullified())
  {
    __ARR* arr;
    for(auto iter=_sections.begin(); iter.IsValid(); ++iter)
    {
      arr=_sections[*iter];
      unsigned int svar=arr->Size();
      for(unsigned int i =0; i<svar; ++i) (*arr)[i].~cINIsection();
      delete arr;
    }
  }
}

void cINIstorage::_copyhash(const cINIstorage& copy)
{
  __ARR* old;
  __ARR* arr;
  for(auto iter=copy._sections.begin(); iter.IsValid(); ++iter)
  {
    old=copy._sections[*iter];
    arr=new __ARR(*old);
    unsigned int svar=arr->Size();
    for(unsigned int i =0; i < svar; ++i)
      new ((*arr)+i) cINIsection((*old)[i]);
    _sections.Insert((*arr)[0].GetName(),arr);
  }
}

cINIstorage& cINIstorage::operator=(const cINIstorage& right)
 { 
   if(&right == this) return *this;
   _path=right._path;
   _filename=right._filename;
   if(_ini!=0) delete _ini;
   _ini=!right._ini?0:new cStr(*right._ini);
   _logger=right._logger;
   _destroyhash();
   _sections.Clear();
   _copyhash(right);
   return *this;
}
cINIstorage& cINIstorage::operator=(cINIstorage&& mov)
{
   if(&mov == this) return *this;
   _path=std::move(mov._path);
   _filename=mov._filename;
   if(_ini!=0) delete _ini;
   _ini=mov._ini;
   mov._ini=0;
   _logger=mov._logger;
   _destroyhash();
   _sections=std::move(mov._sections);
   return *this;
}

/*
void cINIstorage<wchar_t>::_openINI()
{
  cStrW targetpath=_path;
  targetpath+=_filename;
  FILE* f;
  WFOPEN(f,targetpath,L"a+b"); //this will create the file if it doesn't already exist
  if(!f) return;
  fseek(f,0,SEEK_END);
  unsigned long size=ftell(f);
  fseek(f,0,SEEK_SET);
  cStr buf(size+1);
  size=fread(buf.UnsafeString(),sizeof(char),size,f); //reads in the entire file
  buf.UnsafeString()[size]='\0';
  size=UTF8Decode2BytesUnicode(buf,0);
  _ini = new cStrW(size+1);
  UTF8Decode2BytesUnicode(buf,_ini->UnsafeString());
  _ini->UnsafeString()[size]='\0';
  _ini->RecalcSize();
  fclose(f);
}*/

/*void _futfwrite(const wchar_t* source, char discard, int size, FILE* f)
{
  unsigned char* buf = (unsigned char*)malloc(size*sizeof(wchar_t));
  size=UTF8Encode2BytesUnicode(source,buf);
  fwrite(buf,sizeof(unsigned char),size,f);
  free(buf);
}*/