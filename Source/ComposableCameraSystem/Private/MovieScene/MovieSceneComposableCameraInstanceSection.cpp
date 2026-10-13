// Copyright 2026 Sulley. All Rights Reserved.
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrackInstance.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"
#include <type_traits>

#if WITH_EDITOR
FComposableCameraInstanceEditorValue UMovieSceneComposableCameraInstanceSection::OnGetEditorValue;
namespace
{
template<typename T>
TMovieSceneExternalValue<T> InstanceExternalValue(UMovieSceneComposableCameraInstanceSection* Section, FName Input)
{
	TMovieSceneExternalValue<T> Result;
	TWeakObjectPtr<UMovieSceneComposableCameraInstanceSection> WeakSection = Section;
	Result.OnGetExternalValue = [WeakSection, Input](UObject&, FTrackInstancePropertyBindings*) -> TOptional<T>
	{
		if (WeakSection.IsValid() && UMovieSceneComposableCameraInstanceSection::OnGetEditorValue.IsBound())
		{
			const auto Value = UMovieSceneComposableCameraInstanceSection::OnGetEditorValue.Execute(WeakSection.Get(), Input);
			if (Value.IsSet()) return static_cast<T>(Value.GetValue());
		}
		return TOptional<T>(); // Native keying falls back to the evaluated channel value.
	};
	if constexpr (std::is_same_v<T, double>)
	{
		// These channels have a single winning section, not a property-blender entity.
		// Native numeric keying needs the curve's old value to recompose the pending
		// editor value; without this callback it interrogates a nonexistent property system.
		Result.OnGetCurrentValueAndWeight = [WeakSection, Input](UObject*, UMovieSceneSection*, FFrameNumber Time,
			FFrameRate, FMovieSceneRootEvaluationTemplateInstance&, double& OutValue, float& OutWeight)
		{
			OutValue = 0.0;
			OutWeight = 1.f;
			if (const auto* Owner = WeakSection.Get())
				if (const auto* Channel = Owner->Channels.FindByPredicate([Input](const auto& Item)
					{ return Item.Input == Input && Item.Type == EComposableCameraInstanceFieldType::Number; }))
					Channel->Number.Evaluate(Time, OutValue);
		};
	}
	return Result;
}
}
#endif

UMovieSceneComposableCameraInstanceSection::UMovieSceneComposableCameraInstanceSection(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	bSupportsInfiniteRange = true;
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::RestoreState);
	InvalidateChannelProxy();
}

bool UMovieSceneComposableCameraInstanceSection::AddKey(const FComposableCameraInstanceField& Field, FFrameNumber Time, EMovieSceneKeyInterpolation Interpolation)
{
	if (Field.Type == EComposableCameraInstanceFieldType::Object || !Field.Property || !Field.Value || IsLocked()) return false;
	const double Value = Field.ReadNumber();
	if (!FMath::IsFinite(Value) || (Field.Type == EComposableCameraInstanceFieldType::Integer && (Value < MIN_int32 || Value > MAX_int32))) return false;
	FComposableCameraInstanceChannel* Channel = Channels.FindByPredicate([&](const auto& Item) { return Item.Input == Field.Path; });
	if (Channel && Channel->Type != Field.Type) return false; // Preserve orphaned animation after a type change.
	const bool bNewChannel = Channel == nullptr;
	Modify();
	if (!Channel)
	{
		Channel = &Channels.AddDefaulted_GetRef();
		Channel->Input = Field.Path;
		Channel->Type = Field.Type;
	}
	switch (Field.Type)
	{
	case EComposableCameraInstanceFieldType::Number:
	{
		(void)::AddKeyToChannel(&Channel->Number, Time, Value, ::GetInterpolationMode(&Channel->Number, Time, Interpolation));
		break;
	}
	case EComposableCameraInstanceFieldType::Integer: Channel->Integer.GetData().UpdateOrAddKey(Time, static_cast<int32>(Value)); break;
	case EComposableCameraInstanceFieldType::Boolean: Channel->Boolean.GetData().UpdateOrAddKey(Time, Value != 0.0); break;
	default: return false;
	}
	if (bNewChannel) RebuildChannelProxy();
	MarkAsChanged();
	return true;
}

void UMovieSceneComposableCameraInstanceSection::Evaluate(FFrameTime Time, TArrayView<const FComposableCameraInstanceField> Fields) const
{
	for (const FComposableCameraInstanceChannel& Channel : Channels)
	{
		const FComposableCameraInstanceField* Field = Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Channel.Input && Item.Type == Channel.Type; });
		if (!Field) continue;
		double Number = 0.0;
		int32 Integer = 0;
		bool Boolean = false;
		switch (Channel.Type)
		{
		case EComposableCameraInstanceFieldType::Number: if (Channel.Number.Evaluate(Time, Number)) Field->WriteNumber(Number); break;
		case EComposableCameraInstanceFieldType::Integer: if (Channel.Integer.Evaluate(Time, Integer)) Field->WriteNumber(Integer); break;
		case EComposableCameraInstanceFieldType::Boolean: if (Channel.Boolean.Evaluate(Time, Boolean)) Field->WriteNumber(Boolean ? 1.0 : 0.0); break;
		default: break;
		}
	}
}

void UMovieSceneComposableCameraInstanceSection::RebuildChannelProxy()
{
	FMovieSceneChannelProxyData Proxy;
	for (FComposableCameraInstanceChannel& Channel : Channels)
	{
#if WITH_EDITOR
		FMovieSceneChannelMetaData Metadata(Channel.Input, FText::FromName(Channel.Input));
		Metadata.bCanCollapseToTrack = GetOuter()->IsA<UMovieSceneComposableCameraInstanceParameterTrack>() && Channels.Num() == 1;
		switch (Channel.Type)
		{
		case EComposableCameraInstanceFieldType::Number: Proxy.Add(Channel.Number, Metadata, InstanceExternalValue<double>(this, Channel.Input)); break;
		case EComposableCameraInstanceFieldType::Integer: Proxy.Add(Channel.Integer, Metadata, InstanceExternalValue<int32>(this, Channel.Input)); break;
		case EComposableCameraInstanceFieldType::Boolean: Proxy.Add(Channel.Boolean, Metadata, InstanceExternalValue<bool>(this, Channel.Input)); break;
		default: break;
		}
#else
		switch (Channel.Type)
		{
		case EComposableCameraInstanceFieldType::Number: Proxy.Add(Channel.Number); break;
		case EComposableCameraInstanceFieldType::Integer: Proxy.Add(Channel.Integer); break;
		case EComposableCameraInstanceFieldType::Boolean: Proxy.Add(Channel.Boolean); break;
		default: break;
		}
#endif
	}
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Proxy));
	ChannelProxyType = EMovieSceneChannelProxyType::Dynamic;
}

EMovieSceneChannelProxyType UMovieSceneComposableCameraInstanceSection::CacheChannelProxy()
{
	RebuildChannelProxy();
	return EMovieSceneChannelProxyType::Dynamic;
}

void UMovieSceneComposableCameraInstanceSection::PostLoad()
{
	Super::PostLoad();
	// Older parameter sections serialized false. Enable native range controls without
	// extending authored ranges or changing existing keys during load.
	if (GetOuter()->IsA<UMovieSceneComposableCameraInstanceParameterTrack>()) bSupportsInfiniteRange = true;
	RebuildChannelProxy();
}
#if WITH_EDITOR
void UMovieSceneComposableCameraInstanceSection::PostEditUndo() { Super::PostEditUndo(); RebuildChannelProxy(); }
#endif
void UMovieSceneComposableCameraInstanceSection::ImportEntityImpl(UMovieSceneEntitySystemLinker*,
	const UE::MovieScene::FEntityImportParams&, UE::MovieScene::FImportedEntity* Entity)
{
	// Parameter sections are sampled by their instance; they must not create extra camera evaluators.
	if (!GetOuter()->IsA<UMovieSceneComposableCameraInstanceTrack>()) return;
	using namespace UE::MovieScene;
	FMovieSceneTrackInstanceComponent Component{
		decltype(FMovieSceneTrackInstanceComponent::Owner)(this), UMovieSceneComposableCameraInstanceTrackInstance::StaticClass() };
	Entity->AddBuilder(FEntityBuilder().AddTag(FBuiltInComponentTypes::Get()->Tags.Root).Add(FBuiltInComponentTypes::Get()->TrackInstance, Component));
}
