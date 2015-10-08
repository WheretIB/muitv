# muitv [![Build status](https://ci.appveyor.com/api/projects/status/9m9ar7h2y56ri098?svg=true)](https://ci.appveyor.com/project/WheretIB/muitv)

This project provides a dynamically loaded library with a memory allocator that will create and display a tree view of all allocation call stacks in your windowed win32 application.

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
