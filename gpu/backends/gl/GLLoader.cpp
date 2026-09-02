#include <cstdlib>

#define GLAD_MALLOC(size) std::malloc(size)
#define GLAD_FREE(pointer) std::free(pointer)
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
