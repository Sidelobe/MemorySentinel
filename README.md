```
 ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
 ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
 ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘                            
```

### A utility to detect memory allocation and de-allocation in a given scope. Meant to be used for testing environments, not production deployments.


![](https://img.shields.io/github/license/Sidelobe/Hyperbuffer)
![](https://img.shields.io/badge/C++14-blue.svg?style=flat&logo=c%2B%2B)
![](https://img.shields.io/badge/dependencies-STL_only-blue)

The `MemorySentinel` hijacks the all the system's variants of `new` & `delete` as well `malloc` & `free`. When unarmed, it quietly monitors the memory allocation landscape without intervening (quiet infiltration). When armed, and as soon as a "transgression" is detected, the `MemorySentinel` will become active, and either:

* Throw a ` std::bad_alloc` (only for allocation funtions)
* Log to console (while still allocating normally)
* Register the event silently (status can be queried)

#### `MemorySentinel` is very useful in Unit Tests to:

* Monitor dynamic memory allocation during certain function calls / operations
* Simulate out-of-memory conditions (e.g. branch coverage metrics)

#### Allocation Quota
The sentinel's strict zero-allocation-tolerance can be softened by calling `setAllocationQuota(int numBytes)`. If this scenario the sentinel will log allocation until the quota is reached and then behave as usual.

### Usage

```cpp
MemorySentinel& sentinel = MemorySentinel::getInstance();
sentinel.setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
sentinel.setArmed(true);

float* heapObject = new float[32]; // will throw an exception

// After use, disarm Sentinel
sentinel.setArmed(false);
sentinel.clearTransgressions();
```

### Scoped usage

```cpp
{
  ScopedMemorySentinel sentinel;	
  float* heapObject = new float[32];  
} 
// will assert upon exiting scope
```


### Requirements / Compatibility
 - C++14
 - STL
 - tested with GCC, Clang and MSVC
 - tested with Catch2 Test Framework
 - tested on macos, windows & linux

### Status

![](https://img.shields.io/badge/branch-main-blue)
[![Build & Test](https://github.com/Sidelobe/MemorySentinel/actions/workflows/workflow.yml/badge.svg)](https://github.com/Sidelobe/MemorySentinel/actions/workflows/workflow.yml)

MemorySentinel
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=Sidelobe_MemorySentinel)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=coverage)](https://sonarcloud.io/summary/new_code?id=Sidelobe_MemorySentinel)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=bugs)](https://sonarcloud.io/summary/new_code?id=Sidelobe_MemorySentinel)
[![Vulnerabilities](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=vulnerabilities)](https://sonarcloud.io/summary/new_code?id=Sidelobe_MemorySentinel)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=ncloc)](https://sonarcloud.io/summary/new_code?id=Sidelobe_MemorySentinel)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=Sidelobe_MemorySentinel)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=reliability_rating)](https://sonarcloud.io/dashboard?id=Sidelobe_MemorySentinel)
[![Security Rating](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=security_rating)](https://sonarcloud.io/dashboard?id=Sidelobe_MemorySentinel)
[![Technical Debt](https://sonarcloud.io/api/project_badges/measure?project=Sidelobe_MemorySentinel&metric=sqale_index)](https://sonarcloud.io/dashboard?id=Sidelobe_MemorySentinel)

