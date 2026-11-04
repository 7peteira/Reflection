// This code is in the public domain -- castanyo@yahoo.es

#ifndef __linux__

#ifndef NV_IMAGE_H
#define NV_IMAGE_H

#include <nvcore/nvcore.h>

// Export free functions and classes from the ReflectionNVTT module.
// Class members must not also carry NVTT_API when the enclosing class is exported.
#ifndef NVTT_API
#define NVTT_API REFLECTIONNVTT_API
#endif
#ifndef NVIMAGE_API
#define NVIMAGE_API REFLECTIONNVTT_API
#endif
#ifndef NVIMAGE_CLASS
#define NVIMAGE_CLASS REFLECTIONNVTT_API
#endif

#endif // NV_IMAGE_H

#endif // __linux__
