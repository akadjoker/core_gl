// Single translation unit for miniz's implementation. Everywhere else that
// needs the ZIP-reading API (Filesystem.cpp, for .pk3/.zip archives)
// includes miniz.h with MINIZ_HEADER_FILE_ONLY defined first, so the actual
// function bodies only get compiled once, here.
#include "miniz.h"
