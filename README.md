# muitv [![Build status](https://ci.appveyor.com/api/projects/status/9m9ar7h2y56ri098?svg=true)](https://ci.appveyor.com/project/WheretIB/muitv)

This project provides a dynamically loaded library with a low-overhead memory allocator that will create and display a tree view of all allocation call stacks in your win32 application.

![Dashboard example](http://egoengine.com/trash/fp/img_2015_10_08_23_31_26.png)

## Integration

#### Building
* Open vs/muitv.sln solution in Visual Studio 2010+
* Build either debug or release project configuration

#### Using
* Place 'muitv.h' in your project folder and include it in your source file
```
#include "muitv.h"
```
* Place 'muitv.lib' from 'bin' folder to your additional library search folder (project directory by default)
* Add 'muitv.lib' to your project additional library dependencies or import directly from your source file
```
#pragma comment(lib, "muitv.lib")
```
* Place 'muitv.dll' to your application executable folder

## Configuration

####  C library memory allocation override
By default, only global new/delete operators are overloaded by the library. In order to provide instrumentation for C library memory allocation functions, before including the 'muitv.h' header file, provide the following definition:
```
#define MUITV_OVERRIDE_CLIB_MALLOC
```
(Note that this option is currently only available on x86)

#### Library use as an event recorder
As an artifact of the library design, it can be reused to put any call stack to the tree view. In order to create a custom event view, before including the 'muitv.h' header file, provide the following definition:
```
#define MUITV_MANUAL_ONLY
```
Use ```muitv_add_call_stack_to_tree``` function to log events.

## Known issues

#### Interaction with Incremental Linking
When Incremental Linking is enabled, first link after adding the header to your project might fail with messages about unresolved external functions:
```
error LNK2005: "void * __cdecl operator new(unsigned int)" (??2@YAPAXI@Z) already defined in MSVCRTD.lib(MSVCR120D.dll)
error LNK2005: "void __cdecl operator delete(void *)" (??3@YAXPAX@Z) already defined in MSVCRTD.lib(MSVCR120D.dll)
error LNK2005: "void * __cdecl operator new[](unsigned int)" (??_U@YAPAXI@Z) already defined in msvcprtd.lib(newaop_s.obj)
error LNK2005: "void __cdecl operator delete[](void *)" (??_V@YAXPAX@Z) already defined in MSVCRTD.lib(MSVCR120D.dll)
```
Just link your project a second time to resolve this issue.

#### C allocation function override
When ```MUITV_OVERRIDE_CLIB_MALLOC``` is in use, the library will attempt to patch the malloc/calloc/realloc/free functions in memory. Depending on the execution order and contents of global initializers in your application, some allocation might have happened before the override and the application will crash.
