#pragma once

bool CanImport(const FString& Type, const bool IsCloud = false, const UClass* Class = nullptr);

bool ClassIsFormed(const UClass* Class);
