#include "SKSE/SKSE.h"

// Plugin version/name metadata. SKSEPluginInfo defines SKSEPlugin_Version
// (a PluginDeclaration) plus the legacy SKSEPlugin_Query entry.
SKSEPluginInfo(
    .Version = REL::Version{ 1, 0, 0, 0 },
    .Name = "NPCIC",
    .Author = "NPC IC",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 }
);
