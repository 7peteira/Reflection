#pragma once

#include "Engine/AssetCompatibility.h"
#include "Engine/Compatibility.h"
#include "Engine/Notifications.h"
#include "Engine/Package.h"
#include "Engine/Plugin.h"
#include "Engine/Properties.h"

#include "Settings/SettingsAccess.h"
#include "Utilities/ContentBrowser.h"
#include "Utilities/Dialog.h"

#include "Modules/Metadata.h"
#include "Serializers/ObjectSerializer.h"
#include "Serializers/PropertySerializer.h"

#if  !PLATFORM_LINUX
#include "Windows/WindowsHWrapper.h"
#endif
