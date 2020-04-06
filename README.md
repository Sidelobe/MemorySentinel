[![Build Status (master)](https://travis-ci.com/Sidelobe/MemorySentinel.svg?branch=master)](https://travis-ci.com/Sidelobe/MemorySentinel) 
[![Build Status (develop)](https://travis-ci.com/Sidelobe/MemorySentinel.svg?branch=develop)](https://travis-ci.com/Sidelobe/MemorySentinel) 


```
 ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
 ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
 ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘                            
```

A utility to detect memory allocation and de-allocation in a given scope. Meant to be used for testing environments.

If a "transgression" is detected, the `MemorySentinel` will either:

* Throw a `std::exception`
* Log to console
* Register the event silently (status can be queried)

Usage:

```cpp
MemorySentinel& sentinel = MemorySentinel::getInstance();
sentinel.setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
sentinel.setArmed(true);

float* heapObject = new float[32]; // will throw an exception

// After use, disarm Sentinel
sentinel.setArmed(false);
sentinel.clearTransgressions();
```

Scoped usage:

```cpp
{
  ScopedMemorySentinel sentinel;	
  float* heapObject = new float[32];  
} 
// will assert upon exiting scope
```


#### Requirements / Compatibility
 - C++14
 - STL
 - tested with GCC 7.x and Clang 11.x
 - tested with Catch2 Test Framework
