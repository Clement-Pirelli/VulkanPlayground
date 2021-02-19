#pragma once
#include <unordered_map>
#include <optional>
#include "CommonConcepts.h"

template<class Key, typename Value>
class ResourceMap 
{
public:

	Value* get(const Key& key)
	{
        return const_cast<Value*>(const_cast<const ResourceMap<Key, Value>*>(this)->get(key));
	}

    const Value * get(const Key &key) const
    {
        if (auto it = map.find(key);
            it != map.end())
        {
            return &(*it).second;
        }
        else {
            return nullptr;
        }
    }

    template<typename... Args>
    void add(const Key& key, Args&&... args)
    {
        map.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(args...));
    }

    void set(const Key& key, const Value& value)
    {
        map[key] = value;
    }

    template<con::InvocableWith<const Key&, Value&> Operation_t>
    void forEach(Operation_t operation)
    {
        for(auto &pair : map)
        {
            operation(pair.first, pair.second);
        }
    }

private:
	std::unordered_map<Key, Value> map;
};