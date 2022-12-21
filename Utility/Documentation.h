// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

/*!
    \brief Utilities and reusable classes

    This namespace provides many classes and functions used by higher level code. As this code is
    at the very bottom of the layer diagram, it is designed to be as flexible and reusable as possible.

    See below for some of the most commonly used classes and functions.
    
    ## Threading
        Cross-platform interfaces for primitive threading operations (such as mutexes
        and atomic operations.

        Some part of this threading library were implemented before C++11 standard library
        implementations were available. As a result, some parts of this have been superceeded
        by the standard library.
            Class | Description
            ----- | -----------
            CompletionThreadPool | <i>basic thread pool with windows completion ports</i>
            Utility::Interlocked namespace | <i>layer over atomic CPU instructions</i> 

    ## Streams
        IO functionality and streams abstraction

        Includes serialization and deserialisation utilities as well file access and path processing
        utilities.
            Class | Description
            ----- | -----------
            BasicFile | <i>simple file IO layer, used frequently</i>
            InputStreamFormatter | <i>high performance parser of structured text files</i>
            OutputStreamFormatter | <i>output equivalent to InputStreamFormatter</i>
            XmlInputStreamFormatter | <i>load structured data from xml files</i>
            StreamDOM | <i>exposes a "DOM" like interface for data from InputStreamFormatter or XmlInputStreamFormatter</i>
            SplitPath | <i>utility for processing and merging paths</i>
            OutputStream | <i>primitive text-oriented stream interface (similar to std::ostream)</i>
            AttachFileSystemMonitor() | <i>monitor a file for future changes</i>

    ## Profiling
        Currently only includes HierarchicalCPUProfiler, which provides a convenient interface for
        every-day first-step profiling tasks.

    ## Meta
        Metaprogramming utility classes.
            Class | Description
            ----- | -----------
            ClassAccessors | <i>register C# property-like get and set accessors for C++ classes</i>
            AccessorSerialize() and AccessorDeserialize() | <i>Automatic serialization with native classes via ClassAccessors</i>
            CreateFromParameters() | <i>Automatically build a native class from a parameter box</i>
            VariantFunctions | <i>Stores a list of arbitrary functor objects associated by string name</i>

    ## Heap
        Memory management utilities and heap implementations
            Class | Description
            ----- | -----------
            LRUCache | <i>Records a finite subset of the most recently used items of a larger set</i>
            MiniHeap | <i>Moderate performance (but highly flexible) heap implementation. Used for small and special case heap implementations</i>
            SpanningHeap | <i>Heap management utility for arbitrarily sized blocks</i>
            BitHeap | <i>Records allocated/deallocated status for a fixed set of equal heap blocks</i>

    ## Misc
        See also many other reuseable components:
            Class | Description
            ----- | -----------
            ParameterBox | <i>Stores a set of variant types, indexed by string hash</i>
            utf8, ucs2, ucs4 | <i>unicode character type library</i>
            StringMeld | <i>allows ostream::operator<< syntax on a fixed size character buffer</i>
            Hash32(), Hash64(), HashCombine() | <i>general purpose hashing function</i>
            
        Standard library extension functions:
            Class | Description
            ----- | -----------
            Conversion::Convert() | <i>Generic syntax for writing conversion functions</i>
            Equivalent() | <i>Checks for equivalency within a threshold of error</i>
            checked_cast() | <i>static_cast in release, but with an rtti-based assertion in debug</i>
            Default() | <i>returns a default value for a given type</i>
            PrintFormat() | <i>printf-style formatted output to a OutputStream</i>

        The utility library also provides a layer over many standard library functions (eg, XlCopyMemory),
        as well as layers over common OS functions (like GetCommandLine and FindFiles).

*/
namespace Utility {}

