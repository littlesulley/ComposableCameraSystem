// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "Misc/ConfigCacheIni.h"

namespace ComposableCameraSystem::ShotEditorLayout
{
	inline constexpr const TCHAR* ConfigSection = TEXT("ComposableCameraSystem.ShotEditorLayout");
	inline constexpr const TCHAR* FoldSection = TEXT("ComposableCameraSystem.ShotEditorFolds");
	inline float ValidAuthoringFraction(float Value)
	{
		return FMath::IsFinite(Value) && Value >= .15f && Value <= .85f ? Value : .4f;
	}
	inline int32 ValidPage(int32 Value) { return Value >= 0 && Value <= 4 ? Value : 1; }
	inline int32 ValidEditSection(int32 Value) { return Value >= 0 && Value <= 4 ? Value : 0; }

	struct FShotEditorLayoutState
	{
		bool bViewportToolbarCollapsed = false;
		bool bQuickControlsCollapsed = true;
		bool bShowHud = false;
		bool bShowGuides = true;
		float AuthoringFraction = .4f;
		int32 Page = 1;
		int32 EditSection = 0;
	};

	/** User/project view preferences only. Never serialize these into a Shot. */
	inline FShotEditorLayoutState ReadLayoutState(FConfigCacheIni& Config, const FString& Ini)
	{
		FShotEditorLayoutState State;
		Config.GetBool(ConfigSection, TEXT("ViewportToolbarCollapsed"), State.bViewportToolbarCollapsed, Ini);
		Config.GetBool(ConfigSection, TEXT("ShowHud"), State.bShowHud, Ini);
		Config.GetBool(ConfigSection, TEXT("ShowGuides"), State.bShowGuides, Ini);
		Config.GetFloat(ConfigSection, TEXT("AuthoringFraction"), State.AuthoringFraction, Ini);
		Config.GetInt(ConfigSection, TEXT("Page"), State.Page, Ini);
		Config.GetInt(ConfigSection, TEXT("EditSection"), State.EditSection, Ini);
		State.AuthoringFraction = ValidAuthoringFraction(State.AuthoringFraction);
		State.Page = ValidPage(State.Page);
		State.EditSection = ValidEditSection(State.EditSection);
		return State;
	}
	inline void WriteLayoutState(FConfigCacheIni& Config, const FString& Ini, const FShotEditorLayoutState& State)
	{
		Config.SetBool(ConfigSection, TEXT("ViewportToolbarCollapsed"), State.bViewportToolbarCollapsed, Ini);
		Config.SetBool(ConfigSection, TEXT("ShowHud"), State.bShowHud, Ini);
		Config.SetBool(ConfigSection, TEXT("ShowGuides"), State.bShowGuides, Ini);
		Config.SetFloat(ConfigSection, TEXT("AuthoringFraction"), ValidAuthoringFraction(State.AuthoringFraction), Ini);
		Config.SetInt(ConfigSection, TEXT("Page"), ValidPage(State.Page), Ini);
		Config.SetInt(ConfigSection, TEXT("EditSection"), ValidEditSection(State.EditSection), Ini);
	}
	inline bool ReadFold(const FString& Key, bool bDefault, const FString& Ini = GEditorPerProjectIni)
	{
		if (GConfig) GConfig->GetBool(FoldSection, *Key, bDefault, Ini);
		return bDefault;
	}
	inline void WriteFold(const FString& Key, bool bExpanded, const FString& Ini = GEditorPerProjectIni)
	{
		if (GConfig) GConfig->SetBool(FoldSection, *Key, bExpanded, Ini);
	}

	inline FShotEditorLayoutState ResolveLayoutState(
		const TOptional<bool>& ViewportToolbarCollapsed,
		const TOptional<bool>& QuickControlsCollapsed)
	{
		FShotEditorLayoutState State;

		if (ViewportToolbarCollapsed.IsSet())
		{
			State.bViewportToolbarCollapsed = ViewportToolbarCollapsed.GetValue();
		}
		if (QuickControlsCollapsed.IsSet())
		{
			State.bQuickControlsCollapsed = QuickControlsCollapsed.GetValue();
		}

		return State;
	}
}
