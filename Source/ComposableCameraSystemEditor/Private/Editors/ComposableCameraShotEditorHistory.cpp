// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraShotEditorHistory.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "EdGraph/EdGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "UObject/Package.h"

namespace
{
	/** Section name in `GEditorPerProjectIni`. The dotted-namespace style
	 * matches the convention used by other CCS editor settings sections. */
	constexpr const TCHAR* kConfigSection = TEXT("ComposableCameraSystem.ShotEditorHistory");
}

FShotEditorHistory::FShotEditorHistory(bool bInPersist) : bPersist(bInPersist)
{
	Load();
}

FShotEditorHistory& FShotEditorHistory::Get()
{
	static FShotEditorHistory Instance;
	return Instance;
}

void FShotEditorHistory::Push(UObject* Host, const FText& DisplayLabel)
{
	if (!IsHostAvailable(Host) || DisplayLabel.IsEmpty())
	{
		return;
	}

	const FSoftObjectPath HostPath(Host);

	// Dedupe against existing entries that match either the live UObject
	// (typical in-session re-open) or the soft path (cross-restart re-open
	// where the previous entry was loaded from disk and the weak ref is
	// still cold). Removing first preserves the "newest at front"
	// invariant when the user re-opens an old entry.
	Entries.RemoveAll(
		[Host, &HostPath](const FShotEditorHistoryEntry& E)
		{
			if (E.Host.Get() == Host)
			{
				return true;
			}
			return !E.HostPath.IsNull() && E.HostPath == HostPath;
		});

	FShotEditorHistoryEntry New;
	New.Host = Host;
	New.HostPath = HostPath;
	New.DisplayLabel = DisplayLabel;
	Entries.Insert(MoveTemp(New), 0);

	if (Entries.Num() > MaxEntries)
	{
		Entries.SetNum(MaxEntries);
	}

	Save();
}

bool FShotEditorHistory::IsHostAvailable(UObject* Host)
{
	if (!IsValid(Host) || Host->HasAnyFlags(RF_ClassDefaultObject)) return false;
	// Asset deletion clears public/standalone flags before Undo/GC releases objects.
	const UObject* Asset = Host;
	while (Asset->GetOuter() && !Asset->GetOuter()->IsA<UPackage>()) Asset = Asset->GetOuter();
	if (!IsValid(Asset)) return false;
	if (Host->GetOutermost() != GetTransientPackage() && !Asset->IsAsset()) return false;
	if (const auto* Instance = Cast<UComposableCameraInstanceData>(Host))
	{
		const auto* Track = Instance->GetTypedOuter<UMovieSceneComposableCameraInstanceTrack>();
		return IsValid(Track) && Track->Instance == Instance && Track->IsInMovieScene();
	}
	if (const auto* Section = Cast<UMovieSceneComposableCameraShotSection>(Host))
	{
		const auto* Track = Section->GetTypedOuter<UMovieSceneTrack>();
		const auto* Scene = Track ? Track->GetTypedOuter<UMovieScene>() : nullptr;
		if (!IsValid(Track) || !IsValid(Scene) || !Track->HasSection(*Section)) return false;
		if (Scene->GetTracks().Contains(Track)) return true;
		for (const FMovieSceneBinding& Binding : Scene->GetBindings())
			if (Binding.GetTracks().Contains(Track)) return true;
		return false;
	}
	if (const auto* Node = Cast<UComposableCameraCompositionFramingNode>(Host))
	{
		if (const auto* Type = Node->GetTypedOuter<UComposableCameraTypeAsset>())
		{
			// The live graph is authoritative, including disconnected nodes.
			if (Type->EditorGraph)
			{
				for (const UEdGraphNode* GraphNode : Type->EditorGraph->Nodes)
					if (const auto* CameraNode = Cast<UComposableCameraNodeGraphNode>(GraphNode))
						if (CameraNode->NodeTemplate == Node) return true;
				return false;
			}
			return Type->NodeTemplates.Contains(Node);
		}
		const auto* Camera = Node->GetTypedOuter<AComposableCameraCameraBase>();
		return IsValid(Camera) && Camera->CameraNodes.Contains(Node);
	}
	// Scratch drafts have no durable identity and cannot be reopened independently.
	return Host->IsA<UComposableCameraShotAsset>() && Host->IsAsset();
}

UObject* FShotEditorHistory::ResolveHost(const FShotEditorHistoryEntry& Entry)
{
	UObject* Host = Entry.Host.Get();
	// Old ini entries may contain transient names reused by unrelated objects after restart.
	if (!Host && Entry.HostPath.GetLongPackageName() == GetTransientPackage()->GetName()) return nullptr;
	if (!Host) Host = Entry.HostPath.ResolveObject();
	if (!Host && Entry.HostPath.IsValid()
		&& FPackageName::DoesPackageExist(Entry.HostPath.GetLongPackageName()))
	{
		// Only menu opening/clicks load cold paths, never scrubbing or Slate polling.
		Host = Entry.HostPath.TryLoad();
	}
	return IsHostAvailable(Host) ? Host : nullptr;
}

void FShotEditorHistory::PruneUnavailable()
{
	bool bChanged = false;
	Entries.RemoveAll([&bChanged](FShotEditorHistoryEntry& Entry)
	{
		UObject* Host = ResolveHost(Entry);
		if (!Host || Entry.DisplayLabel.IsEmpty()) { bChanged = true; return true; }
		const FSoftObjectPath CurrentPath(Host);
		bChanged |= Entry.HostPath != CurrentPath;
		Entry.Host = Host;
		Entry.HostPath = CurrentPath;
		return false;
	});
	if (bChanged) Save();
}

void FShotEditorHistory::Clear()
{
	Entries.Reset();
	if (bPersist && GConfig)
	{
		GConfig->EmptySection(kConfigSection, GEditorPerProjectIni);
		GConfig->Flush(/*bRead=*/false, GEditorPerProjectIni);
	}
}

void FShotEditorHistory::Load()
{
	Entries.Reset();
	if (!bPersist || !GConfig)
	{
		return;
	}

	// Contiguous `PathN` / `LabelN` pairs. First missing key pair signals
	// end-of-list - manual ini edits that punch a hole in the middle just
	// truncate the list there, no crash.
	for (int32 i = 0; i < MaxEntries; ++i)
	{
		const FString PathKey = FString::Printf(TEXT("Path%d"), i);
		const FString LabelKey = FString::Printf(TEXT("Label%d"), i);

		FString PathStr;
		FString LabelStr;
		const bool bHasPath = GConfig->GetString(kConfigSection, *PathKey, PathStr, GEditorPerProjectIni);
		const bool bHasLabel = GConfig->GetString(kConfigSection, *LabelKey, LabelStr, GEditorPerProjectIni);
		if (!bHasPath || !bHasLabel)
		{
			break;
		}

		FShotEditorHistoryEntry E;
		E.HostPath = FSoftObjectPath(PathStr);
		E.DisplayLabel = FText::FromString(LabelStr);
		// Host weak ref intentionally left empty - the asset isn't loaded
		// yet. PruneUnavailable resolves it when Recent is opened.
		Entries.Add(MoveTemp(E));
	}
}

void FShotEditorHistory::Save()
{
	if (!bPersist || !GConfig)
	{
		return;
	}

	// Wipe the section before re-writing so old keys past the current
	// length don't linger (e.g., list shrinks via Clear or eviction).
	GConfig->EmptySection(kConfigSection, GEditorPerProjectIni);

	int32 SavedIndex = 0;
	for (const FShotEditorHistoryEntry& E : Entries)
	{
		if (!E.HostPath.IsValid() || E.DisplayLabel.IsEmpty())
		{
			continue;
		}
		const UObject* Host = E.Host.Get();
		if (E.HostPath.GetLongPackageName() == GetTransientPackage()->GetName()
			|| (Host && (Host->HasAnyFlags(RF_Transient) || Host->GetOutermost()->HasAnyFlags(RF_Transient)))) continue;
		const FString PathKey = FString::Printf(TEXT("Path%d"), SavedIndex);
		const FString LabelKey = FString::Printf(TEXT("Label%d"), SavedIndex++);
		GConfig->SetString(kConfigSection, *PathKey,
			*E.HostPath.ToString(), GEditorPerProjectIni);
		GConfig->SetString(kConfigSection, *LabelKey,
			*E.DisplayLabel.ToString(), GEditorPerProjectIni);
	}

	GConfig->Flush(/*bRead=*/false, GEditorPerProjectIni);
}
