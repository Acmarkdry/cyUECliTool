// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/Material.h"

extern TMap<FString, UClass*> ExpressionClassMap;
extern TMap<FString, EMaterialShadingModel> ShadingModelMap;
extern TMap<FString, EBlendMode> BlendModeMap;

void InitExpressionClassMap();
void InitShadingModelMap();
void InitBlendModeMap();
