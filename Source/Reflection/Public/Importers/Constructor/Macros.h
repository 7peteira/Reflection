#pragma once

#include "Engine/EngineUtilities.h"

#define REGISTER_IMPORTER_OVERRIDE(ImporterClass, AcceptedTypes, Category) \
namespace { \
    struct FAutoOverride_##ImporterClass { \
        FAutoOverride_##ImporterClass() { \
            FImporterRegistrationInfo Info(FString(Category), &CreateImporter<ImporterClass>); \
            GetFactoryOverrides().Add(AcceptedTypes, Info); \
        } \
    }; \
    static FAutoOverride_##ImporterClass AutoOverride_##ImporterClass; \
}

#define REGISTER_IMPORTER(ImporterClass, AcceptedTypes, Category) \
namespace { \
    struct FAutoRegister_##ImporterClass { \
        FAutoRegister_##ImporterClass() { \
            FImporterRegistrationInfo Info(FString(Category), &CreateImporter<ImporterClass>); \
            GetFactoryRegistry().Add(AcceptedTypes, Info); \
        } \
    }; \
    static FAutoRegister_##ImporterClass AutoRegister_##ImporterClass; \
}
