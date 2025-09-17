// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/MiniHeap.h"
#include "../Utility/IteratorUtils.h"
#include <vector>

namespace SceneEngine
{
    class PreparedScene
    {
    public:
        using Id = uint64_t;

        template<typename Type, typename... Args> Type* Allocate(Id id, Args... args);
        template<typename Type> Type* Get(Id id = 0);

        PreparedScene();
        PreparedScene(PreparedScene&&);
        ~PreparedScene();
        PreparedScene& operator=(PreparedScene&&);
    private:
        MiniHeap _heap;

        using Destructor = void(void*);
        class Block
        {
        public:
            Id _id;
            MiniHeap::Allocation _allocation;
            Destructor* _destructor;
        };
        std::vector<std::pair<size_t, Block>> _blocks;

        template<typename Type> static void DestructorImpl(void* ptr)
            { ((Type*)ptr)->~Type(); }
    };

    template<typename Type, typename... Args> 
        Type* PreparedScene::Allocate(Id id, Args... args)
        {
            auto r = std::equal_range(
                _blocks.begin(), _blocks.end(),
                typeid(Type).hash_code(), CompareFirst<size_t, Block>());
            for (auto i=r.first; i!=r.second; ++i) {
                assert(i->second._id != id);
            }

            #pragma push_macro("new")
            #undef new
                auto alloc = _heap.Allocate(sizeof(Type));
                new(alloc._allocation) Type(args...);
            #pragma pop_macro("new")

            Destructor* destr = &DestructorImpl<Type>;
            _blocks.insert(r.second, std::make_pair(typeid(Type).hash_code(), Block{ id, alloc, destr }));
            return (Type*)alloc._allocation;
        }

    template<typename Type> 
        Type* PreparedScene::Get(Id id)
        {
            auto r = std::equal_range(
                _blocks.begin(), _blocks.end(),
                typeid(Type).hash_code(), CompareFirst<size_t, Block>());
            for (auto i=r.first; i!=r.second; ++i)
                if (i->second._id == id)
                    return (Type*)i->second._allocation._allocation;
            return nullptr;
        }
}

