// Single translation unit for stb_image implementations.
// Avoids duplicate-symbol errors when multiple .cpp files include stb headers.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
