#pragma once

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>

#include <cstdio>
#include <dlfcn.h>

#include <functional>
#include <string>

template <class FType>
struct OriginalFunction{

  OriginalFunction(std::string const& funcName, std::string const& libN){
    dl_handle = dlopen(libN.c_str(), RTLD_LAZY);
    if(dl_handle == nullptr){
      fprintf(stderr, "Failed to open library %s\n", libN.c_str());
      return;
    }
    func = (FType*)dlsym(dl_handle, funcName.c_str());
    if(func == nullptr){
      fprintf(stderr, "Failed to find function %s\n", funcName.c_str());
      return;
    }
  }

  ~OriginalFunction(){
    if(dl_handle != nullptr){
      dlclose(dl_handle);
    }
  }

  template <class... Args>
  auto operator()(Args... args){
    return func(args...);
  }

  std::function<FType> func;
  void* dl_handle;
  
}; // struct OriginalFunction

using XShmAttachType = Bool(Display*, XShmSegmentInfo*);
inline auto XShmAttachFunc = OriginalFunction<XShmAttachType>{"XShmAttach", "libXext.so"};

using XShmDetachType = Bool(Display*, XShmSegmentInfo*);
inline auto XShmDetachFunc = OriginalFunction<XShmDetachType>{"XShmDetach", "libXext.so"};

using XShmGetImageType = Bool(Display*, Drawable, XImage*, int, int, unsigned long);
inline auto XShmGetImageFunc = OriginalFunction<XShmGetImageType>{"XShmGetImage", "libXext.so"};

using XDamageQueryExtensionType = Bool(Display*, int*, int*);
inline auto XDamageQueryExtensionFunc = OriginalFunction<XDamageQueryExtensionType>{"XDamageQueryExtension", "libXdamage.so"};