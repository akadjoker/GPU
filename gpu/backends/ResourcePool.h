#ifndef GPU_RESOURCE_POOL_H
#define GPU_RESOURCE_POOL_H

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>

namespace gpu
{

  template <typename Object, typename HandleType>
  class ResourcePool
  {
    static_assert(std::is_trivially_copyable<Object>::value,
                  "ResourcePool objects must be trivially copyable");

    struct Entry
    {
      Object object{};
      std::uint32_t generation = 1;
      std::uint32_t nextFree = InvalidIndex;
      bool alive = false;
    };

  public:
    ResourcePool() = default;
    ResourcePool(const ResourcePool &) = delete;
    ResourcePool &operator=(const ResourcePool &) = delete;

    ~ResourcePool() { std::free(mEntries); }

    HandleType insert(const Object &object)
    {
      std::uint32_t index = InvalidIndex;
      if (mFirstFree != InvalidIndex)
      {
        index = mFirstFree;
        mFirstFree = mEntries[index].nextFree;
      }
      else
      {
        if (mSize == mCapacity && !grow())
          return HandleType();
        index = mSize++;
        new (&mEntries[index]) Entry();
      }
      Entry &entry = mEntries[index];
      entry.object = object;
      entry.nextFree = InvalidIndex;
      entry.alive = true;
      ++mLiveCount;
      return HandleType((static_cast<std::uint64_t>(entry.generation) << 32) |
                        static_cast<std::uint64_t>(index + 1));
    }

    Object *find(HandleType handle)
    {
      const std::uint64_t value = handle.value();
      const std::uint32_t index = static_cast<std::uint32_t>(value) - 1;
      const std::uint32_t generation = static_cast<std::uint32_t>(value >> 32);
      if (!handle.valid() || index >= mSize || !mEntries[index].alive ||
          mEntries[index].generation != generation)
        return nullptr;
      return &mEntries[index].object;
    }

    const Object *find(HandleType handle) const
    {
      return const_cast<ResourcePool *>(this)->find(handle);
    }

    bool erase(HandleType handle)
    {
      const std::uint32_t index = static_cast<std::uint32_t>(handle.value()) - 1;
      if (!find(handle))
        return false;
      Entry &entry = mEntries[index];
      entry.object = Object{};
      entry.alive = false;
      ++entry.generation;
      if (entry.generation == 0)
        entry.generation = 1;
      entry.nextFree = mFirstFree;
      mFirstFree = index;
      --mLiveCount;
      return true;
    }

    template <typename Function>
    void forEach(Function function)
    {
      for (std::uint32_t index = 0; index < mSize; ++index)
        if (mEntries[index].alive)
          function(mEntries[index].object);
    }

    void clear()
    {
      std::free(mEntries);
      mEntries = nullptr;
      mSize = 0;
      mCapacity = 0;
      mFirstFree = InvalidIndex;
      mLiveCount = 0;
    }

    std::uint32_t size() const { return mLiveCount; }
    std::uint32_t capacity() const { return mCapacity; }

  private:
    static constexpr std::uint32_t InvalidIndex =
        (std::numeric_limits<std::uint32_t>::max)();

    bool grow()
    {
      if (mCapacity == InvalidIndex)
        return false;
      const std::uint32_t nextCapacity =
          mCapacity == 0
              ? 64
              : (mCapacity > InvalidIndex / 2 ? InvalidIndex : mCapacity * 2);
      if (nextCapacity >
          (std::numeric_limits<std::size_t>::max)() / sizeof(Entry))
        return false;
      void *memory =
          std::realloc(mEntries, static_cast<std::size_t>(nextCapacity) *
                                     sizeof(Entry));
      if (!memory)
        return false;
      mEntries = static_cast<Entry *>(memory);
      mCapacity = nextCapacity;
      return true;
    }

    Entry *mEntries = nullptr;
    std::uint32_t mSize = 0;
    std::uint32_t mCapacity = 0;
    std::uint32_t mFirstFree = InvalidIndex;
    std::uint32_t mLiveCount = 0;
  };

} // namespace gpu

#endif
