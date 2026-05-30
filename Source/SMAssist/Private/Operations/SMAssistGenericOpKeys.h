// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

// The ld_ue.* namespace is a FALLBACK-ONLY generic Unreal Engine surface: alternatives to other
// engine MCP tools (blueprint.*, ui.*), to be used only when those fail or cannot express the
// operation. These are not Logic Driver operations.
namespace LD::Assist::GenericOps::Ops
{
	inline constexpr const TCHAR* ReadProperty = TEXT("ld_ue.read_property");
	inline constexpr const TCHAR* WriteProperty = TEXT("ld_ue.write_property");
	inline constexpr const TCHAR* AddDispatcher = TEXT("ld_ue.add_dispatcher");
}

namespace LD::Assist::GenericOps::Args
{
	inline constexpr const TCHAR* Object = TEXT("object");
	inline constexpr const TCHAR* Target = TEXT("target");
	inline constexpr const TCHAR* PropertyPath = TEXT("property_path");
	inline constexpr const TCHAR* Value = TEXT("value");
	inline constexpr const TCHAR* PieInstance = TEXT("pie_instance");

	inline constexpr const TCHAR* ObjectResolved = TEXT("object_resolved");
	inline constexpr const TCHAR* PropertyType = TEXT("property_type");

	inline constexpr const TCHAR* AssetPath = TEXT("asset_path");
	inline constexpr const TCHAR* Name = TEXT("name");
	inline constexpr const TCHAR* Params = TEXT("params");
	inline constexpr const TCHAR* ParamName = TEXT("name");
	inline constexpr const TCHAR* ParamType = TEXT("type");
	inline constexpr const TCHAR* DispatcherName = TEXT("dispatcher_name");
	inline constexpr const TCHAR* ParamsApplied = TEXT("params_applied");
}
