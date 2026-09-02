#include "ResourcePool.h"
#include "gpu/GPUHandles.h"

#include <cassert>
#include <cstdint>

namespace
{

  struct Object
  {
    std::uint32_t value = 0;
  };

  void testGrowthAndLookup()
  {
    gpu::ResourcePool<Object, gpu::BufferHandle> pool;
    gpu::BufferHandle handles[1024];
    for (std::uint32_t index = 0; index < 1024; ++index)
    {
      handles[index] = pool.insert(Object{index});
      assert(handles[index].valid());
      assert(pool.find(handles[index])->value == index);
    }
    assert(pool.size() == 1024);
    assert(pool.capacity() >= 1024);
  }

  void testReuseAndGeneration()
  {
    gpu::ResourcePool<Object, gpu::TextureHandle> pool;
    const gpu::TextureHandle first = pool.insert(Object{1});
    assert(pool.erase(first));
    assert(pool.size() == 0);
    assert(pool.find(first) == nullptr);
    const gpu::TextureHandle second = pool.insert(Object{2});
    assert(second.valid());
    assert(second != first);
    assert(pool.find(first) == nullptr);
    assert(pool.find(second)->value == 2);
    assert(pool.size() == 1);
  }

  void testIterationAndClear()
  {
    gpu::ResourcePool<Object, gpu::SamplerHandle> pool;
    const gpu::SamplerHandle removed = pool.insert(Object{3});
    pool.insert(Object{5});
    pool.insert(Object{7});
    assert(pool.erase(removed));
    std::uint32_t sum = 0;
    pool.forEach([&sum](Object &object) { sum += object.value; });
    assert(sum == 12);
    assert(pool.size() == 2);
    pool.clear();
    assert(pool.size() == 0);
    assert(pool.capacity() == 0);
  }

}

int main()
{
  testGrowthAndLookup();
  testReuseAndGeneration();
  testIterationAndClear();
  return 0;
}
