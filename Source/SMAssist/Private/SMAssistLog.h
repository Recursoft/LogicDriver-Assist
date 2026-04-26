// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Stats/Stats.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLogicDriverAssist, Log, All);

#define LDASSIST_LOG_INFO(FMT, ...) UE_LOG(LogLogicDriverAssist, Log, (FMT), ##__VA_ARGS__)
#define LDASSIST_LOG_WARNING(FMT, ...) UE_LOG(LogLogicDriverAssist, Warning, (FMT), ##__VA_ARGS__)
#define LDASSIST_LOG_ERROR(FMT, ...) UE_LOG(LogLogicDriverAssist, Error, (FMT), ##__VA_ARGS__)

DECLARE_STATS_GROUP(TEXT("LogicDriverAssist"), STATGROUP_LogicDriverAssist, STATCAT_Advanced)
