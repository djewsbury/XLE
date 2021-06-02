
set(Src
    ArithmeticUtils.cpp
    BitUtils.cpp
    Conversion.cpp
    FastParseValue.cpp
    FunctionUtils.cpp
    HashUtils.cpp
    HeapUtils.cpp
    MiniHeap.cpp
    MiscImplementation.cpp
    ParameterBox.cpp
    StringFormat.cpp
    StringFormatTime.cpp
    StringUtils.cpp
    UTFUtils.cpp
    ImpliedTyping.cpp
    xl_snprintf.cpp)
set(Headers
    ArithmeticUtils.h
    BitUtils.h
    Conversion.h
    Documentation.h
    FastParseValue.h
    FunctionUtils.h
    HeapUtils.h
    IntrusivePtr.h
    IteratorUtils.h
    MemoryUtils.h
    MiniHeap.h
    Optional.h
    ParameterBox.h
    ParameterPackUtils.h
    PtrUtils.h
    StreamUtils.h
    StringFormat.h
    StringUtils.h
    UTFUtils.h
    VariantUtils.h)
set(StreamsSrc 
    Streams/ConditionalPreprocessingTokenizer.cpp
    Streams/Data.cpp
    Streams/DataSerialize.cpp
    Streams/PathUtils.cpp
    Streams/PreprocessorInterpreter.cpp
    Streams/Stream.cpp
    Streams/StreamDOM.cpp
    Streams/StreamFormatter.cpp
    Streams/XmlStreamFormatter.cpp)
set(ThreadingSrc Threading/CompletionThreadPool.cpp)
set(ProfilingSrc Profiling/CPUProfiler.cpp Profiling/SuppressionProfiler.cpp)
set(MetaSrc Meta/AccessorSerialize.cpp Meta/ClassAccessors.cpp)

add_library(Utility STATIC ${Src} ${Headers} ${StreamsSrc} ${ThreadingSrc} ${ProfilingSrc} ${MetaSrc})
xle_configure_library(Utility)
target_link_libraries(Utility PUBLIC ForeignMisc)

if (WIN32)
    target_link_libraries(Utility PRIVATE wsock32.lib ws2_32.lib)
elseif (NOT APPLE)
    target_link_libraries(Utility PRIVATE pthread)
endif ()
