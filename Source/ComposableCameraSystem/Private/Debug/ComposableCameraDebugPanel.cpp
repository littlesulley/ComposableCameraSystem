// Copyright Sulley. All rights reserved.

#include "Debug/ComposableCameraDebugPanel.h"

#include "Actions/ComposableCameraActionBase.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "CanvasItem.h"
#include "CineCameraComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraModifierManager.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Debug/ComposableCameraLogCapture.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "Debug/DebugDrawService.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Patches/ComposableCameraPatchManager.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "TextureResource.h"   // GWhiteTexture — needed as the shading source for FCanvasTriangleItem
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "UObject/UObjectIterator.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Slate/SceneViewport.h"
#include "Utils/ComposableCameraDebugFormatUtils.h"
#include "Widgets/SViewport.h"

// ─────────────────────────────────────────────────────────────────────
// Debug panel — private implementation.
// Public surface is only Initialize() / Shutdown(); everything else
// lives in an anonymous namespace in this translation unit.
// ─────────────────────────────────────────────────────────────────────
namespace
{
	// ---- Console variables ---------------------------------------------
	static TAutoConsoleVariable<int32> CVarPanelEnabled(
		TEXT("CCS.Debug.Panel"),
		0,
		TEXT("Toggle the in-viewport debug panel for ComposableCameraSystem.\n")
		TEXT("  0: disabled (draw delegate early-outs)\n")
		TEXT("  1: enabled (panel drawn every frame on every local viewport)"),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarPanelWidth(
		TEXT("CCS.Debug.Panel.Width"),
		0.32f,
		TEXT("Fraction of screen width occupied by the panel (clamped to 0.15-0.60)."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarPanelLegend(
		TEXT("CCS.Debug.Panel.Legend"),
		1,
		TEXT("Show the color legend region at the bottom of the debug panel.\n")
		TEXT("  0: legend region always hidden\n")
		TEXT("  1: legend shown when at least one viewport debug CVar\n")
		TEXT("     (CCS.Debug.Viewport.*) is enabled — lists a color swatch\n")
		TEXT("     and label for each currently-drawing gizmo so the user\n")
		TEXT("     can match screen colors to node/transition names."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarPanelWarnings(
		TEXT("CCS.Debug.Panel.Warnings"),
		1,
		TEXT("Show the Warnings region listing the most recent\n")
		TEXT("LogComposableCamera* warnings / errors captured by\n")
		TEXT("FComposableCameraLogCapture.\n")
		TEXT("  0: region always hidden\n")
		TEXT("  1: region shown when at least one entry is in the ring buffer\n")
		TEXT("     (entries age off naturally as new warnings arrive)."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarPanelPatches(
		TEXT("CCS.Debug.Panel.Patches"),
		1,
		TEXT("Show the Patches region listing every active CameraPatch on the\n")
		TEXT("active context's Director. Three lines per patch:\n")
		TEXT("   [layer=N] AssetName   <Phase>\n")
		TEXT("     a=0.42  enter 0.10/0.25s\n")
		TEXT("     expire: D+CamChange\n")
		TEXT("  0: region always hidden\n")
		TEXT("  1: region always shown (uses '(none)' placeholder when empty)."),
		ECVF_Default);

	// ---- Module state --------------------------------------------------
	// Delegate handles for the two debug panels live down near the
	// `Initialize`/`Shutdown` implementation so the dual-channel
	// (Game + Editor) registration scheme is co-located with its
	// dedup wrapper. See the big comment block above `Initialize`.

	// ---- Pose History panel CVars --------------------------------------
	static TAutoConsoleVariable<int32> CVarPoseHistoryEnabled(
		TEXT("CCS.Debug.Panel.PoseHistory"),
		0,
		TEXT("Toggle the right-side Pose History panel.\n")
		TEXT("  0: disabled (draw delegate early-outs)\n")
		TEXT("  1: enabled — draws 6 sparkline rows (Pos.X/Y/Z, Rot.P/Y/R)\n")
		TEXT("     showing the last ~2 seconds of pose history, plus a\n")
		TEXT("     mouse-hover scrub cursor that pops a tooltip with the\n")
		TEXT("     pose at the hovered frame."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarPoseHistoryWidth(
		TEXT("CCS.Debug.Panel.PoseHistory.Width"),
		0.28f,
		TEXT("Fraction of screen width occupied by the Pose History panel\n")
		TEXT("(clamped to 0.15-0.50)."),
		ECVF_Default);

	// ---- Visual constants ---------------------------------------------
	// Alpha budget note: translucent canvas tiles COMPOUND — two 0.85-alpha
	// layers stacked over the same pixel produce ~0.98 effective coverage,
	// which reads as opaque. The panel therefore uses a single outer BG fill
	// (content area has exactly one translucent layer = KPanelBGAlpha), with
	// an extra small fill only for the title bar (compounds to ~0.80).
	//
	// Palette: title bar uses the same hue family as the border (muted lilac)
	// at a slightly darker shade so it reads as "backdrop", not "edge".
	static const FLinearColor CPanelBG      (0.03f, 0.03f, 0.05f, 0.55f);
	static const FLinearColor CTitleBG      (0.28f, 0.22f, 0.42f, 0.70f);
	static const FLinearColor CBorder       (0.45f, 0.35f, 0.70f, 0.80f);
	static const FLinearColor CTitle        (0.98f, 0.88f, 1.00f, 1.00f);
	static const FLinearColor CLabel        (0.55f, 0.85f, 0.95f, 1.00f);
	static const FLinearColor CValue        (1.00f, 0.85f, 0.35f, 1.00f);
	static const FLinearColor CNeutral      (0.90f, 0.90f, 0.90f, 1.00f);
	static const FLinearColor CLeaf         (0.55f, 0.95f, 0.55f, 1.00f);
	static const FLinearColor CTransition   (1.00f, 0.85f, 0.35f, 1.00f);
	static const FLinearColor CRefLeaf      (1.00f, 0.65f, 0.25f, 1.00f);
	static const FLinearColor CDestroyed    (0.75f, 0.35f, 0.35f, 1.00f);
	static const FLinearColor CActiveMarker (0.40f, 1.00f, 1.00f, 1.00f);

	static constexpr float KMargin         = 12.f;  // panel outer margin from screen edge
	static constexpr float KPadding        = 6.f;   // region inner padding (left/right/top/bottom of content)
	static constexpr float KTitleBarH      = 18.f;  // title bar height per region
	static constexpr float KInterRegionGap = 4.f;   // vertical gap between regions
	static constexpr float KLineH          = 13.f;  // body line height (matches small font approx.)

	// ---- Primitive helpers --------------------------------------------
	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Pos, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	static void DrawBorder(UCanvas* Canvas, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color, float Thickness = 1.f)
	{
		FCanvasBoxItem Box(Pos, Size);
		Box.SetColor(Color);
		Box.LineThickness = Thickness;
		Canvas->DrawItem(Box);
	}

	/** Measure a string's pixel width in the given font. Empty string → 0. */
	static float MeasureTextWidth(UCanvas* Canvas, UFont* Font, const FString& Text)
	{
		if (Text.IsEmpty()) { return 0.f; }
		float XL = 0.f, YL = 0.f;
		Canvas->StrLen(Font, Text, XL, YL);
		return XL;
	}

	/** Draw text at (X, Y), truncating with "..." if it would exceed MaxX pixels
	 *  on the right. Uses a binary search over the input string length so long
	 *  strings converge in O(log n * StrLen). Degenerate case (MaxX too narrow
	 *  even for "...") silently drops the line. */
	static float DrawTextLineClipped(UCanvas* Canvas, UFont* Font, const FString& InText,
		float X, float Y, float MaxX, const FLinearColor& Color)
	{
		const float MaxWidth = MaxX - X;
		if (MaxWidth <= 0.f || InText.IsEmpty())
		{
			return Y + KLineH;
		}

		const float FullWidth = MeasureTextWidth(Canvas, Font, InText);
		FString Text = InText;
		if (FullWidth > MaxWidth)
		{
			static const TCHAR* Ellipsis = TEXT("...");
			const float EllipsisWidth = MeasureTextWidth(Canvas, Font, Ellipsis);
			if (EllipsisWidth >= MaxWidth)
			{
				return Y + KLineH; // not enough room for anything meaningful
			}
			// Binary search: longest prefix that still fits with "..." appended.
			int32 Lo = 0;
			int32 Hi = InText.Len();
			while (Lo < Hi)
			{
				const int32 Mid = (Lo + Hi + 1) / 2;
				const FString Candidate = InText.Left(Mid) + Ellipsis;
				if (MeasureTextWidth(Canvas, Font, Candidate) <= MaxWidth)
				{
					Lo = Mid;
				}
				else
				{
					Hi = Mid - 1;
				}
			}
			Text = InText.Left(Lo) + Ellipsis;
		}

		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
		return Y + KLineH;
	}

	// ─────────────────────────────────────────────────────────────────
	// Region definition + renderers.
	// Each region is a static function that draws into a pre-computed
	// content rect. Regions are sized by a sibling "height estimator"
	// that inspects the PCM's live state.
	// ─────────────────────────────────────────────────────────────────
	struct FPanelCtx
	{
		UCanvas* Canvas                              = nullptr;
		const AComposableCameraPlayerCameraManager* PCM = nullptr;
		UFont* HeaderFont                            = nullptr;
		UFont* BodyFont                              = nullptr;
	};

	/** A single text row inside a region body. Rows come in two shapes:
	 *
	 *    1. Full-line: `Label` is empty, `Value` holds the complete rendered
	 *       string. Used for section headers (`-- Data Block --`), placeholder
	 *       rows (`(none)`), and any row with no meaningful key/value split.
	 *
	 *    2. Key/value: both `Label` and `Value` non-empty. The region renderer
	 *       measures the pixel width of every Label in the region once, and
	 *       draws each Value aligned to `BodyX + MaxLabelPx + KLabelValueGap`
	 *       — so values line up under each other regardless of how wide the
	 *       individual labels are. This is the only way to get clean
	 *       vertical alignment in a proportional font; the previous approach
	 *       of padding labels with spaces in the source only aligned on
	 *       monospace fonts, which the debug panel does not use.
	 *
	 *  Two implicit constructors let call sites keep using brace-init:
	 *    Out.Lines.Add({ TEXT("…"), Color });            // full-line
	 *    Out.Lines.Add({ TEXT("Label"), TEXT("Value"), Color }); // KV
	 */
	struct FPanelLine
	{
		FString      Label;
		FString      Value;
		FLinearColor Color;

		FPanelLine(FString InText, const FLinearColor& InColor)
			: Label(), Value(MoveTemp(InText)), Color(InColor)
		{}

		FPanelLine(FString InLabel, FString InValue, const FLinearColor& InColor)
			: Label(MoveTemp(InLabel)), Value(MoveTemp(InValue)), Color(InColor)
		{}
	};

	/** One labelled group inside the Current Pose region. The Pose region
	 *  doesn't use the flat `Lines` path — it packs groups into two columns
	 *  to keep the panel compact (a single column would push every other
	 *  region ~15 lines further down). Each group renders as a `-- Header --`
	 *  label followed by its own KV lines with per-group value alignment. */
	struct FPoseGroup
	{
		FString            Header;
		TArray<FPanelLine> Lines;
	};

	/** Per-region render data. Most regions emit simple text lines (Lines).
	 *  The Context Stack & Tree region uses a structured snapshot instead
	 *  (bIsStackAndTree = true, StackBodyHeight pre-computed) so it can
	 *  draw progress bars, tree connector lines, and active-leaf highlights.
	 *  The Legend region also uses a structured path (bIsLegend = true) so
	 *  it can paint color swatches next to labels in a two-column layout.
	 *  The Current Pose region uses `bIsPose = true` to flow labelled groups
	 *  across two columns (PoseGroups[0..PoseLeftGroupCount) go in the left
	 *  column; the rest in the right). */
	struct FRegionLines
	{
		FString Title;
		TArray<FPanelLine> Lines;

		// Structured-region fields (Context Stack & Tree only).
		bool  bIsStackAndTree = false;
		float StackBodyHeight = 0.f;

		// Legend region.
		bool  bIsLegend       = false;
		float LegendBodyHeight = 0.f;

		// Current Pose region.
		bool  bIsPose            = false;
		TArray<FPoseGroup> PoseGroups;
		float PoseBodyHeight     = 0.f;
		int32 PoseLeftGroupCount = 0;

		// Patches region — structured like Stack & Tree so each patch can
		// render a phase-colored identity row + two filled progress bars
		// (Alpha / Time) instead of plain text. The snapshot is stashed here
		// at build time and the renderer walks it in DrawPatchesStructured.
		bool  bIsPatches          = false;
		TArray<FComposableCameraPatchSnapshot> PatchSnapshots;
		float PatchesBodyHeight   = 0.f;

		// Warnings region reuses the plain Lines path — no new flag needed.
		// Each line carries its own verbosity-colored FLinearColor, which
		// is exactly what Lines was built to express.
	};

	// Visual constants for the structured tree render.
	static constexpr float KTreeIndentPx           = 12.f;  // px added per depth level
	static constexpr float KTreeGutterInset        = 6.f;   // px from region left edge to first gutter
	static constexpr float KTreeGutterThick        = 1.f;   // px stem thickness
	static constexpr float KTreeElbowInset         = 4.f;   // px between elbow end and text start

	// Row height for InnerTransition rows — taller than KLineH so the row
	// can fit the timing-curve sparkline below the text line. Other row
	// kinds (Leaf / ReferenceLeaf) stay at KLineH.
	//
	// 36 gives the curve area ~22 px of effective height, an order of
	// magnitude more readable than the original 22-row-height iteration
	// where the curve was squashed into ~7 px of vertical space and
	// reduced every curve shape to an indistinguishable faint line.
	static constexpr float KTreeTransitionLineH    = 36.f;
	// Vertical padding inside the transition row that separates the text
	// line from the curve area. Keeps the curve visually "below" the text
	// rather than hugging its descenders.
	static constexpr float KTreeCurveTopPad        = 2.f;
	// Maximum horizontal width of the curve area, regardless of how much
	// space the row has. Without this cap the curve gets stretched across
	// the full row width on wide panels, blowing the aspect ratio into a
	// ~1:20+ flat landscape where no shape reads distinctly. Cap at 220
	// so the curve stays in a ~1:10 aspect — narrow-ish, but legible.
	// The whitespace to the right of the curve is intentional; users
	// already read "how far along" from the inline percentage text and
	// from the amber-vs-grey boundary in the columns themselves.
	static constexpr float KTreeCurveMaxWidth      = 220.f;
	// Number of curve-sample segments stored in the snapshot. The snapshot
	// stores NumSamples + 1 values; this is the segment count used by the
	// renderer's loops.
	static constexpr int32 KTreeCurveSampleCount   = 24;

	static const FLinearColor CTreeGutter     (0.55f, 0.45f, 0.75f, 0.55f);
	static const FLinearColor CProgressBar    (0.75f, 0.55f, 0.15f, 0.35f); // amber underlay (progress), translucent
	static const FLinearColor CCurveOutline   (1.00f, 0.92f, 0.70f, 0.90f); // warm cream — curve outline
	static const FLinearColor CCurveAhead     (0.55f, 0.50f, 0.65f, 0.35f); // muted — "not yet reached" curve portion
	static const FLinearColor CDominantBar    (0.35f, 0.80f, 0.55f, 0.25f); // green underlay for the dominant leaf
	static const FLinearColor CBulletInactive (0.60f, 0.55f, 0.80f, 0.90f); // muted lilac for non-active context bullets

	/** Row height for a single tree-node entry in the stack/tree region.
	 *  InnerTransition rows are taller so the timing-curve sparkline fits
	 *  under the text line; every other kind stays at KLineH. Mirrors the
	 *  same logic `ComputeStackBodyHeight` and `DrawStackAndTreeStructured`
	 *  both need, so factored into one helper.
	 *
	 *  `bShowCurve` reflects whether THIS context is the active (top-of-stack)
	 *  one — during cross-context transitions the source/pending and target
	 *  contexts would otherwise both render full-height curve rows, doubling
	 *  the visual noise. Only the active context shows curves; every other
	 *  context collapses InnerTransition rows to a single-line progress
	 *  readout at `KLineH`. */
	static float GetTreeNodeRowHeight(const FComposableCameraTreeNodeSnapshot& TN, bool bShowCurve)
	{
		return (bShowCurve && TN.Kind == EComposableCameraTreeNodeKind::InnerTransition)
			? KTreeTransitionLineH
			: KLineH;
	}

	// ---- Region: Current Pose -----------------------------------------
	/** Populate the Current Pose region as four labelled groups, laid out
	 *  across two columns by the renderer:
	 *    Left column  — Transform, Context
	 *    Right column — Projection, Physical
	 *
	 *  Left/right split is controlled by `PoseLeftGroupCount`. The Physical
	 *  group collapses to a single `Status: off` line when the pose's
	 *  `PhysicalCameraBlendWeight <= 0` (the common case), so the right
	 *  column stays short unless DoF / auto-exposure are actually active.
	 *
	 *  All reads go through the same public pose / POV accessors the
	 *  single-column version used — no new state, no hot-path allocation
	 *  concerns beyond the existing debug-panel baseline. */
	static void BuildPoseGroups(const FPanelCtx& Ctx, FRegionLines& Out)
	{
		Out.Title   = TEXT("Current Pose");
		Out.bIsPose = true;

		const FComposableCameraPose& Pose = Ctx.PCM->CurrentCameraPose;
		const FMinimalViewInfo POV = Ctx.PCM->GetCameraCacheView();

		auto AddGroup = [&](const TCHAR* HeaderStr) -> FPoseGroup&
		{
			FPoseGroup& G = Out.PoseGroups.AddDefaulted_GetRef();
			G.Header = HeaderStr;
			return G;
		};

		TStringBuilder<128> B;

		// ---- Transform (left) ----
		{
			FPoseGroup& Transform = AddGroup(TEXT("Transform"));

			B.Reset(); ComposableCameraDebug::AppendVector(B, Pose.Position);
			Transform.Lines.Add({ TEXT("Position"), FString(B), CValue });

			B.Reset(); ComposableCameraDebug::AppendRotator(B, Pose.Rotation);
			Transform.Lines.Add({ TEXT("Rotation"), FString(B), CValue });

			B.Reset(); ComposableCameraDebug::AppendVector(B, Pose.Rotation.Vector());
			Transform.Lines.Add({ TEXT("Forward"), FString(B), CValue });
		}

		// ---- Context (left) ----
		{
			FPoseGroup& Context = AddGroup(TEXT("Context"));

			B.Reset();
			if (Ctx.PCM->CurrentContext.IsNone())
			{
				B.Append(TEXT("None"));
			}
			else
			{
				Ctx.PCM->CurrentContext.AppendString(B);
			}
			Context.Lines.Add({ TEXT("Active"), FString(B), CValue });
		}

		Out.PoseLeftGroupCount = Out.PoseGroups.Num();

		// ---- Projection (right) ----
		{
			FPoseGroup& Proj = AddGroup(TEXT("Projection"));

			const bool bOrtho = (Pose.ProjectionMode == ECameraProjectionMode::Orthographic);
			Proj.Lines.Add({ TEXT("Mode"),
				FString(bOrtho ? TEXT("Orthographic") : TEXT("Perspective")),
				CValue });

			// FOV dual-mode: show degrees + parenthetical focal length when
			// the pose is in FocalLength mode (sentinel: FieldOfView <= 0).
			B.Reset();
			B.Appendf(TEXT("%.1f"), Pose.GetEffectiveFieldOfView());
			if (Pose.FieldOfView <= 0.0 && Pose.FocalLength > 0.f)
			{
				B.Appendf(TEXT("  (from %.0fmm)"), Pose.FocalLength);
			}
			Proj.Lines.Add({ TEXT("FOV"), FString(B), CValue });

			B.Reset(); B.Appendf(TEXT("%.3f"), POV.AspectRatio);
			Proj.Lines.Add({ TEXT("Aspect"), FString(B), CValue });

			if (bOrtho)
			{
				B.Reset(); B.Appendf(TEXT("%.0f"), Pose.OrthographicWidth);
				Proj.Lines.Add({ TEXT("Ortho W"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("%.0f"), Pose.OrthoNearClipPlane);
				Proj.Lines.Add({ TEXT("Ortho Near"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("%.0f"), Pose.OrthoFarClipPlane);
				Proj.Lines.Add({ TEXT("Ortho Far"), FString(B), CValue });
			}
		}

		// ---- Physical (right) ----
		//
		// Three-way data source. The pose's Physical block is the "CCS-side
		// physical contribution gate" — it only carries meaningful values
		// when a LensNode (or similar) has driven `PhysicalCameraBlendWeight`
		// above 0. In the proxy-via-CineCamera path (e.g. Level Sequence
		// Camera Cut Track → LS Actor → ViewTargetProxyNode writes the pose),
		// the CineCamera already bakes DoF / exposure into PostProcessSettings
		// itself, so the pose's BlendWeight stays 0 and the raw Aperture /
		// Focus / ISO etc. fields keep their struct defaults (not real values).
		// Showing those defaults would be misleading. Instead we detect the
		// CineCamera on the PCM's current view target and read physical info
		// straight off it.
		//
		//   1. `PhysicalCameraBlendWeight > 0`     → pose-driven (6 rows).
		//   2. BlendWeight == 0 AND ViewTarget has UCineCameraComponent
		//                                           → CineCamera-driven (6 rows).
		//   3. neither                              → single "Status: off" row.
		{
			const bool bPoseDriven = (Pose.PhysicalCameraBlendWeight > 0.f);

			UCineCameraComponent* CineCam = nullptr;
			if (!bPoseDriven)
			{
				if (AActor* ViewTarget = Ctx.PCM->GetViewTarget())
				{
					CineCam = ViewTarget->FindComponentByClass<UCineCameraComponent>();
				}
			}

			// Header discriminator — the "(CineCamera)" suffix makes the
			// data source visible at a glance without spending an extra
			// full row on a "Source:" line.
			FPoseGroup& Phys = AddGroup(
				bPoseDriven ? TEXT("Physical")
				: (CineCam  ? TEXT("Physical (CineCamera)")
				            : TEXT("Physical")));

			if (bPoseDriven)
			{
				B.Reset(); B.Appendf(TEXT("%.2f"), Pose.PhysicalCameraBlendWeight);
				Phys.Lines.Add({ TEXT("Weight"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("f/%.1f"), Pose.Aperture);
				Phys.Lines.Add({ TEXT("Aperture"), FString(B), CValue });

				B.Reset();
				if (Pose.FocusDistance > 0.f)
				{
					B.Appendf(TEXT("%.0f cm"), Pose.FocusDistance);
				}
				else
				{
					B.Append(TEXT("auto"));
				}
				Phys.Lines.Add({ TEXT("Focus"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("%.0f"), Pose.ISO);
				Phys.Lines.Add({ TEXT("ISO"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("1/%.0fs"), Pose.ShutterSpeed);
				Phys.Lines.Add({ TEXT("Shutter"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("%.1f x %.1f mm"), Pose.SensorWidth, Pose.SensorHeight);
				Phys.Lines.Add({ TEXT("Sensor"), FString(B), CValue });
			}
			else if (CineCam)
			{
				// Focal length is in the Physical group on this branch (not
				// the Projection group's `FOV (from Nmm)` annotation) because
				// the proxy writes FOV in degrees-mode via SetFieldOfViewDegrees
				// — the pose no longer knows the underlying focal length, but
				// the CineCamera still does and it's physical-optics info.
				B.Reset(); B.Appendf(TEXT("%.1f mm"), CineCam->CurrentFocalLength);
				Phys.Lines.Add({ TEXT("Focal"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("f/%.1f"), CineCam->CurrentAperture);
				Phys.Lines.Add({ TEXT("Aperture"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("%.0f cm"), CineCam->CurrentFocusDistance);
				Phys.Lines.Add({ TEXT("Focus"), FString(B), CValue });

				// ISO / Shutter on a CineCamera come from PostProcessSettings
				// overrides. No override = engine default / auto-exposure —
				// show "auto" rather than the uninitialized numeric slot.
				const FPostProcessSettings& PP = CineCam->PostProcessSettings;

				B.Reset();
				if (PP.bOverride_CameraISO) { B.Appendf(TEXT("%.0f"), PP.CameraISO); }
				else                        { B.Append(TEXT("auto")); }
				Phys.Lines.Add({ TEXT("ISO"), FString(B), CValue });

				B.Reset();
				if (PP.bOverride_CameraShutterSpeed) { B.Appendf(TEXT("1/%.0fs"), PP.CameraShutterSpeed); }
				else                                 { B.Append(TEXT("auto")); }
				Phys.Lines.Add({ TEXT("Shutter"), FString(B), CValue });

				B.Reset(); B.Appendf(TEXT("%.1f x %.1f mm"),
					CineCam->Filmback.SensorWidth,
					CineCam->Filmback.SensorHeight);
				Phys.Lines.Add({ TEXT("Sensor"), FString(B), CValue });
			}
			else
			{
				// Collapsed form — pose has no physical contribution and the
				// view target isn't a CineCamera we could mine data from.
				Phys.Lines.Add({ TEXT("Status"), TEXT("off"), CNeutral });
			}
		}

		// Body height = tallest column. Each group contributes
		// 1 (header) + Lines.Num() lines of KLineH.
		auto GroupLineCount = [](const FPoseGroup& G) -> int32
		{
			return 1 + G.Lines.Num();
		};
		int32 LeftLines = 0;
		int32 RightLines = 0;
		for (int32 i = 0; i < Out.PoseGroups.Num(); ++i)
		{
			const int32 N = GroupLineCount(Out.PoseGroups[i]);
			if (i < Out.PoseLeftGroupCount) { LeftLines  += N; }
			else                            { RightLines += N; }
		}
		Out.PoseBodyHeight = KLineH * static_cast<float>(FMath::Max(LeftLines, RightLines));
	}

	// ---- Region: Context Stack & Evaluation Tree (structured) ---------
	/** Height of the structured tree body, in pixels. One line per context
	 *  header, then per-node rows using `GetTreeNodeRowHeight` (taller for
	 *  InnerTransition rows to fit the timing-curve sparkline). */
	static float ComputeStackBodyHeight(const FComposableCameraContextStackSnapshot& Snapshot)
	{
		if (Snapshot.Contexts.Num() == 0)
		{
			return KLineH; // "(empty)" placeholder line
		}
		float H = 0.f;
		for (const FComposableCameraContextSnapshot& Ctxt : Snapshot.Contexts)
		{
			H += KLineH; // context header
			const bool bShowCurve = Ctxt.bIsActive;
			for (const FComposableCameraTreeNodeSnapshot& TN : Ctxt.TreeNodes)
			{
				H += GetTreeNodeRowHeight(TN, bShowCurve);
			}
		}
		return H;
	}

	/** Draw a leaf/inner/ref-leaf label, including the InnerTransition row's
	 *  timing-curve sparkline and progress fill, plus the active-leaf highlight.
	 *
	 *  Layout of an InnerTransition row (RowH = KTreeTransitionLineH = 22):
	 *    [ LineY                    ] text line (KLineH = 13 tall)
	 *    [ LineY + KLineH + pad ↓   ] curve area: amber area-under-curve up
	 *                                 to TransitionProgress + cream outline
	 *                                 polyline across the whole [0..1] span
	 *
	 *  Leaf / ReferenceLeaf rows are single-line (RowH = KLineH) — they only
	 *  get the dominant-leaf highlight underlay. `RowH` lets the function
	 *  know how much vertical space it owns without re-deriving it.
	 *
	 *  Disambiguation policy (unchanged):
	 *   - Tree connectors (├ └ │) are drawn by the caller as geometric rects.
	 *   - RefLeaf uses `[ref]` label prefix. Dominant leaf is green underlay
	 *     + CActiveMarker text color. No `*` / `->` glyphs overloaded. */
	static void DrawTreeNodeLine(
		UCanvas* Canvas,
		UFont* Font,
		const FComposableCameraTreeNodeSnapshot& TN,
		float LineY,
		float RowH,
		float TextX,
		float RightX,
		bool bShowCurve)
	{
		if (TN.Kind == EComposableCameraTreeNodeKind::InnerTransition && bShowCurve)
		{
			// Transition row: reserve KLineH at top for text; use the extra
			// space below for the blend-curve sparkline + progress fill.
			const float CurveTop     = LineY + KLineH + KTreeCurveTopPad;
			const float CurveBottom  = LineY + RowH - 1.f;         // 1px padding off the bottom edge
			const float CurveHeight  = FMath::Max(2.f, CurveBottom - CurveTop);
			const float CurveLeftX   = TextX;
			const float CurveRightX  = RightX - 4.f;               // match progress-bar right inset
			const float CurveWidth   = FMath::Max(0.f, CurveRightX - CurveLeftX);

			if (CurveWidth > 0.f && TN.BlendCurveSamples.Num() == KTreeCurveSampleCount + 1)
			{
				const float Progress  = FMath::Clamp(TN.TransitionProgress, 0.f, 1.f);
				const float ProgressX = CurveLeftX + CurveWidth * Progress;
				const float SampleColumnW = CurveWidth / static_cast<float>(KTreeCurveSampleCount);

				// 1. Continuous area-under-curve fill via trapezoidal
				//    triangles. Each of the 24 sample intervals becomes
				//    one trapezoid (top = the curve segment between
				//    samples[i] and samples[i+1]; bottom = CurveBottom),
				//    rendered as two triangles submitted together in
				//    one FCanvasTriangleItem. Produces a genuine
				//    continuous-looking fill — the top edge is literally
				//    the piecewise-linear reconstruction of the snapshot
				//    curve, no staircasing. 24 intervals × 2 tris = 48
				//    tris per transition per frame; if the interval
				//    containing ProgressX splits for the amber/ahead
				//    boundary, up to 4 tris for that one interval (so
				//    max 50 tris overall).
				//
				//    Two colors: amber left of ProgressX ("reached"),
				//    muted grey-purple right of it ("ahead"). Hard split
				//    at ProgressX with an interpolated Y-midpoint so the
				//    boundary reads as a sharp vertical edge following
				//    the curve.
				TArray<FCanvasUVTri, TInlineAllocator<64>> FillTris;
				FillTris.Reserve(KTreeCurveSampleCount * 2 + 2);

				const FLinearColor FillReached = CProgressBar;
				const FLinearColor FillAhead   = CCurveAhead;

				auto PushTri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
				{
					FCanvasUVTri Tri;
					Tri.V0_Pos = A; Tri.V0_UV = FVector2D::ZeroVector; Tri.V0_Color = Color;
					Tri.V1_Pos = B; Tri.V1_UV = FVector2D::ZeroVector; Tri.V1_Color = Color;
					Tri.V2_Pos = C; Tri.V2_UV = FVector2D::ZeroVector; Tri.V2_Color = Color;
					FillTris.Add(Tri);
				};
				auto PushTrapezoid = [&](float XL, float YL, float XR, float YR, const FLinearColor& Color)
				{
					// Trapezoid TL (XL, YL) - TR (XR, YR) - BR (XR, Bottom) - BL (XL, Bottom).
					// Split into two triangles (TL, TR, BR) and (TL, BR, BL).
					const FVector2D TL(XL, YL);
					const FVector2D TR(XR, YR);
					const FVector2D BL(XL, CurveBottom);
					const FVector2D BR(XR, CurveBottom);
					PushTri(TL, TR, BR, Color);
					PushTri(TL, BR, BL, Color);
				};

				for (int32 i = 0; i < KTreeCurveSampleCount; ++i)
				{
					const float X0 = CurveLeftX + i * SampleColumnW;
					const float X1 = CurveLeftX + (i + 1) * SampleColumnW;
					const float Y0 = CurveBottom - FMath::Clamp(TN.BlendCurveSamples[i],     0.f, 1.f) * CurveHeight;
					const float Y1 = CurveBottom - FMath::Clamp(TN.BlendCurveSamples[i + 1], 0.f, 1.f) * CurveHeight;

					// A tiny epsilon guards against numerical noise when
					// ProgressX lands exactly on a sample boundary.
					constexpr float KSplitEps = 0.01f;
					if (X1 <= ProgressX + KSplitEps)
					{
						PushTrapezoid(X0, Y0, X1, Y1, FillReached);
					}
					else if (X0 >= ProgressX - KSplitEps)
					{
						PushTrapezoid(X0, Y0, X1, Y1, FillAhead);
					}
					else
					{
						// Split the interval at ProgressX — two trapezoids,
						// meeting at (ProgressX, Ymid) where Ymid is the
						// linear interpolation of Y0..Y1 at ProgressX.
						const float T    = (ProgressX - X0) / (X1 - X0);
						const float Ymid = FMath::Lerp(Y0, Y1, T);
						PushTrapezoid(X0,         Y0,   ProgressX, Ymid, FillReached);
						PushTrapezoid(ProgressX,  Ymid, X1,        Y1,   FillAhead);
					}
				}

				if (FillTris.Num() > 0)
				{
					// FCanvasTriangleItem takes a default TArray — copy
					// the inline-allocated list once here. Cheap; triangle
					// count is bounded by ~50.
					TArray<FCanvasUVTri> TriList(FillTris);
					FCanvasTriangleItem TriItem(TriList, GWhiteTexture);
					TriItem.BlendMode = SE_BLEND_Translucent;
					Canvas->DrawItem(TriItem);
				}

				// 2. Curve outline — thin polyline on top so the line
				//    reads clearly above the filled area. The outline is
				//    the same set of 24 segments the trapezoid tops use,
				//    so the two are pixel-aligned.
				// Two-pass fake AA: wider translucent halo + crisp opaque core.
				// FCanvasLineItem has no native AA, stacking the halo hides stair-stepping.
				const FLinearColor OutlineHalo =
					CCurveOutline.CopyWithNewOpacity(CCurveOutline.A * 0.35f);
				for (int32 i = 0; i < KTreeCurveSampleCount; ++i)
				{
					const float X0 = CurveLeftX + i * SampleColumnW;
					const float X1 = CurveLeftX + (i + 1) * SampleColumnW;
					const float Y0 = CurveBottom - FMath::Clamp(TN.BlendCurveSamples[i],     0.f, 1.f) * CurveHeight;
					const float Y1 = CurveBottom - FMath::Clamp(TN.BlendCurveSamples[i + 1], 0.f, 1.f) * CurveHeight;

					FCanvasLineItem Halo(FVector2D(X0, Y0), FVector2D(X1, Y1));
					Halo.SetColor(OutlineHalo);
					Halo.LineThickness = 2.8f;
					Canvas->DrawItem(Halo);

					FCanvasLineItem Core(FVector2D(X0, Y0), FVector2D(X1, Y1));
					Core.SetColor(CCurveOutline);
					Core.LineThickness = 1.2f;
					Canvas->DrawItem(Core);
				}
			}
			else if (TN.TransitionProgress >= 0.f)
			{
				// Fallback for the rare case the snapshot wasn't populated
				// with curve samples (shouldn't happen post-Step-3, but
				// defend against it): fall back to the old flat amber bar
				// behind the text so users still see SOME progress signal.
				const float Progress = FMath::Clamp(TN.TransitionProgress, 0.f, 1.f);
				const float BarW = FMath::Max(0.f, (RightX - TextX - 4.f) * Progress);
				if (BarW > 0.f)
				{
					DrawFilledRect(Canvas,
						FVector2D(TextX, LineY + 1.f),
						FVector2D(BarW, KLineH - 2.f),
						CProgressBar);
				}
			}
		}
		else if (TN.bIsDominantLeaf && !TN.bDestroyed)
		{
			DrawFilledRect(Canvas,
				FVector2D(TextX, LineY + 1.f),
				FVector2D(FMath::Max(0.f, RightX - TextX - 4.f), KLineH - 2.f),
				CDominantBar);
		}

		// Role within parent transition.
		//   In the ACTIVE tree the ONLY parent type is Inner (transition), so
		//   any non-root node is necessarily a child of a transition. Its role
		//   ("source" vs "target") is trivially `Left → bIsLastSibling=false`,
		//   `Right → bIsLastSibling=true` — no extra snapshot field needed.
		//   Root nodes (Depth == 0) have no role and print without prefix.
		//   EXCEPTION: when a RefLeaf inlines its referenced subtree, the
		//   direct child of the RefLeaf (tagged `bIsReferencedRoot`) sits at
		//   Depth > 0 but its parent is a ReferenceLeaf, not a transition —
		//   so no source/target role applies and the prefix is suppressed.
		const TCHAR* RolePrefix = TEXT("");
		if (TN.Depth > 0 && !TN.bIsReferencedRoot)
		{
			RolePrefix = TN.bIsLastSibling ? TEXT("[to] ") : TEXT("[from] ");
		}

		// Compose body text + pick color.
		FString Text;
		FLinearColor LineColor = CNeutral;
		switch (TN.Kind)
		{
			case EComposableCameraTreeNodeKind::Leaf:
			{
				Text = FString::Printf(TEXT("%s[leaf] %s"), RolePrefix, *TN.DisplayLabel);
				if (TN.bIsTransient)
				{
					Text += FString::Printf(TEXT("  (%.1f/%.1fs)"), TN.LifeElapsed, TN.LifeTotal);
				}
				LineColor = TN.bDestroyed
					? CDestroyed
					: (TN.bIsDominantLeaf ? CActiveMarker : CLeaf);
				break;
			}
			case EComposableCameraTreeNodeKind::ReferenceLeaf:
			{
				Text = FString::Printf(TEXT("%s[ref] %s"), RolePrefix, *TN.DisplayLabel);
				LineColor = TN.bDestroyed ? CDestroyed : CRefLeaf;
				break;
			}
			case EComposableCameraTreeNodeKind::InnerTransition:
			{
				if (TN.TransitionProgress >= 0.f)
				{
					Text = FString::Printf(TEXT("%s%s   %.0f%%  (%.2f/%.2fs)"),
						RolePrefix,
						*TN.DisplayLabel,
						TN.TransitionProgress * 100.f,
						TN.TransitionElapsed,
						TN.TransitionTotal);
				}
				else
				{
					Text = FString::Printf(TEXT("%s%s"), RolePrefix, *TN.DisplayLabel);
				}
				LineColor = TN.bDestroyed ? CDestroyed : CTransition;
				break;
			}
		}

		// Inlined referenced subtree (nodes flattened from a RefLeaf's
		// SnapshotRoot) renders at reduced brightness so the reader sees it
		// as "historical / source-side", not as part of the active tree.
		// Lerp toward the panel's neutral text color so every kind's hue
		// stays recognizable (green leaves stay greenish, amber transitions
		// stay amber-ish), just less vivid.
		if (TN.bInReferencedSubtree)
		{
			LineColor = FMath::Lerp(LineColor, CNeutral, 0.45f);
		}

		DrawTextLineClipped(Canvas, Font, Text, TextX, LineY, RightX, LineColor);
	}

	/** Render the Context Stack & Tree region using a structured snapshot.
	 *  Draws context headers, flattened DFS tree nodes, indentation stems,
	 *  elbow connectors, transition progress bars, and dominant-leaf highlight. */
	static void DrawStackAndTreeStructured(
		const FPanelCtx& Ctx,
		const FComposableCameraContextStackSnapshot& Snapshot,
		const FVector2D& BodyPos,
		const FVector2D& BodySize)
	{
		UCanvas* Canvas = Ctx.Canvas;
		UFont*   Font   = Ctx.BodyFont;
		const float X      = BodyPos.X;
		const float RightX = BodyPos.X + BodySize.X;
		const float MaxY   = BodyPos.Y + BodySize.Y;
		float Y = BodyPos.Y;

		if (Snapshot.Contexts.Num() == 0)
		{
			if (Y + KLineH <= MaxY)
			{
				DrawTextLineClipped(Canvas, Font, TEXT("(empty)"), X, Y, RightX, CDestroyed);
			}
			return;
		}

		// Bullet width reserved on every context-header line so active /
		// inactive / pending lines all start their text at the same X and
		// the reader can compare names without visual drift.
		const float KHeaderBulletW   = 6.f;
		const float KHeaderTextInset = 12.f;

		for (const FComposableCameraContextSnapshot& Ctxt : Snapshot.Contexts)
		{
			if (Y + KLineH > MaxY) { break; }

			// Only the active (top-of-stack) context renders transition
			// timing-curve sparklines. During a cross-context transition
			// the source/pending and target contexts would otherwise both
			// draw full-height curves for the same blend, doubling the
			// visual noise in the panel. Non-active contexts still show
			// their InnerTransition rows, just as single-line progress
			// readouts (same height as a leaf row).
			const bool bShowCurve = Ctxt.bIsActive;

			// Context bullet: every context gets one for visual alignment, color
			// encodes state (pending → red, active → cyan, live-inactive → lilac).
			// The constant left inset means context names line up vertically
			// across the entire stack, and state reads from the bullet alone.
			FLinearColor BulletColor;
			if (Ctxt.bIsPendingDestroy) { BulletColor = CDestroyed; }
			else if (Ctxt.bIsActive)    { BulletColor = CActiveMarker; }
			else                        { BulletColor = CBulletInactive; }
			DrawFilledRect(Canvas,
				FVector2D(X, Y + FMath::RoundToFloat(KLineH * 0.3f)),
				FVector2D(KHeaderBulletW, FMath::RoundToFloat(KLineH * 0.4f)),
				BulletColor);

			// Context header text.
			FString Header;
			FLinearColor HeaderColor;
			if (Ctxt.bIsPendingDestroy)
			{
				Header = FString::Printf(TEXT("[pending] %s"), *Ctxt.ContextName.ToString());
				HeaderColor = CDestroyed;
			}
			else
			{
				const TCHAR* BaseMark = Ctxt.bIsBase ? TEXT("  (base)") : TEXT("");
				Header = FString::Printf(TEXT("%s%s"), *Ctxt.ContextName.ToString(), BaseMark);
				HeaderColor = Ctxt.bIsActive ? CActiveMarker : CNeutral;
			}
			DrawTextLineClipped(Canvas, Font, Header, X + KHeaderTextInset, Y, RightX, HeaderColor);
			Y += KLineH;

			// Tree nodes (DFS pre-order, flattened). Draw proper geometric
			// `├ / └ / │` connectors using bIsLastSibling + AncestorLastFlagsBitmask.
			//
			//   Rules:
			//     - For each column L in [0, Depth-2]: draw a continuation stem
			//       `│` iff bit (L+1) of AncestorLastFlagsBitmask is 0, i.e. the
			//       ancestor at depth L+1 was NOT the last child of its parent.
			//     - At column Depth-1: draw the node's own connector:
			//         └ (last sibling): half-height stem top→mid + horizontal mid tick
			//         ├ (middle child): full-height stem top→bottom + horizontal mid tick
			//     - Root (Depth == 0): no connectors.
			for (const FComposableCameraTreeNodeSnapshot& TN : Ctxt.TreeNodes)
			{
				// Row height depends on node kind — InnerTransition rows
				// are taller because they host the blend-curve sparkline,
				// but only in the active context (see `bShowCurve` above).
				const float RowH = GetTreeNodeRowHeight(TN, bShowCurve);
				if (Y + RowH > MaxY) { break; }

				// Connector mid point stays aligned with the TEXT center
				// (which always sits in the top KLineH of the row), not
				// with the row center. That way the elbow tick points at
				// the text on tall transition rows too.
				const float TextMidY = Y + FMath::RoundToFloat(KLineH * 0.5f);

				// Continuation stems for ancestors still "in progress".
				// Stretch to the full row height so tall transition rows
				// don't leave a visual gap in the vertical stem.
				for (int32 L = 0; L + 1 < TN.Depth; ++L)
				{
					const float StemX = X + KTreeGutterInset + L * KTreeIndentPx;
					if (StemX + KTreeGutterThick > RightX) { break; } // past right edge
					const bool bAncestorAtLPlus1Last =
						(TN.AncestorLastFlagsBitmask & (1u << (L + 1))) != 0;
					if (!bAncestorAtLPlus1Last)
					{
						DrawFilledRect(Canvas,
							FVector2D(StemX, Y),
							FVector2D(KTreeGutterThick, RowH),
							CTreeGutter);
					}
				}

				// Own connector at column Depth-1 — guard against narrow panels.
				if (TN.Depth > 0)
				{
					const float StemX = X + KTreeGutterInset + (TN.Depth - 1) * KTreeIndentPx;
					if (StemX + KTreeGutterThick <= RightX)
					{
						// bIsLastSibling (└): stem from top down to text mid.
						// Middle child (├): stem spans the full row height.
						const float StemH = TN.bIsLastSibling ? (TextMidY - Y) : RowH;
						DrawFilledRect(Canvas,
							FVector2D(StemX, Y),
							FVector2D(KTreeGutterThick, StemH),
							CTreeGutter);
						// Elbow tick — aligned with text mid, clipped to available width.
						const float ElbowMaxW = FMath::Min(KTreeIndentPx, RightX - StemX);
						if (ElbowMaxW > 0.f)
						{
							DrawFilledRect(Canvas,
								FVector2D(StemX, TextMidY),
								FVector2D(ElbowMaxW, KTreeGutterThick),
								CTreeGutter);
						}
					}
				}

				const float TextX = X + KTreeGutterInset + TN.Depth * KTreeIndentPx + KTreeElbowInset;
				DrawTreeNodeLine(Canvas, Font, TN, Y, RowH, TextX, RightX, bShowCurve);
				Y += RowH;
			}
		}
	}

	// ---- Region: Running Camera ---------------------------------------
	static void BuildRunningCameraLines(const FPanelCtx& Ctx, FRegionLines& Out)
	{
		const AComposableCameraCameraBase* Camera = Ctx.PCM->GetRunningCamera();
		Out.Title = TEXT("Running Camera");
		if (!Camera)
		{
			Out.Lines.Add({ TEXT("(none)"), CDestroyed });
			return;
		}

		const UComposableCameraTypeAsset* TypeAsset = Camera->SourceTypeAsset.Get();
		const FString DisplayName = TypeAsset
			? TypeAsset->GetName()
			: (Camera->CameraTag.IsValid() ? Camera->CameraTag.ToString() : Camera->GetName());
		Out.Lines.Add({ TEXT("Class"), DisplayName, CValue });
		Out.Lines.Add({ TEXT("Tag"),
			Camera->CameraTag.IsValid() ? Camera->CameraTag.ToString() : FString(TEXT("(none)")),
			CValue });
		if (Camera->IsTransient())
		{
			Out.Lines.Add({ TEXT("Life"),
				FString::Printf(TEXT("%.2f / %.2fs remaining"),
					Camera->GetRemainingLifeTime(), Camera->GetLifeTime()),
				CValue });
		}

		// Node list — walked in EXECUTION ORDER, not CameraNodes-array index
		// order. The camera's tick loop in `AComposableCameraCameraBase::TickCamera`
		// walks `FullExecChain` (editor-built from graph exec-pin wiring) when
		// it's populated, so the Panel's display matches what the runtime
		// actually does. Falls back to linear CameraNodes order when
		// FullExecChain is absent — matches the same fallback in TickCamera
		// so legacy / non-type-asset cameras still show correctly.
		//
		// Step numbers ([1], [2], ...) are 1-based execution positions, NOT
		// CameraNodes array indices — an earlier iteration used the array
		// index which caused user confusion when the graph's exec wiring
		// ordered nodes differently from how they were added to the array.
		//
		// SetVariable exec-chain entries are shown as indented subtle lines
		// (→ arrow) interleaved between node steps so the user can see
		// "after this node runs, copy its output pin into this variable"
		// — matching the camera's tick interleave exactly.

		const bool bHasDataBlock = Camera->OwnedRuntimeDataBlock && Camera->OwnedRuntimeDataBlock->IsValid();
		const FComposableCameraRuntimeDataBlock* DBForOutputs = bHasDataBlock
			? Camera->OwnedRuntimeDataBlock.Get()
			: nullptr;

		// Helper: emit one node header line + its output pin lines into Out.Lines.
		// Extracted so both the exec-chain walk and the fallback walk call
		// identical code.
		auto EmitNodeLines = [&](int32 StepNum, int32 NodeArrayIdx, const UComposableCameraCameraNodeBase* Node)
		{
			Out.Lines.Add({ FString::Printf(TEXT("  [%2d] %s"), StepNum,
				*Node->GetClass()->GetDisplayNameText().ToString()), CNeutral });

			// Emit output-pin values for this node. GatherAllPinDeclarations
			// is non-const on the base (it calls the BlueprintNativeEvent
			// GetPinDeclarations), so const_cast — same pattern editor-side
			// SnapshotDebugState uses. The call is semantically read-only,
			// it just fills the OutPins array we pass in.
			if (!DBForOutputs) { return; }

			TArray<FComposableCameraNodePinDeclaration> Pins;
			const_cast<UComposableCameraCameraNodeBase*>(Node)->GatherAllPinDeclarations(Pins);

			TStringBuilder<192> B;
			for (const FComposableCameraNodePinDeclaration& Pin : Pins)
			{
				if (Pin.Direction != EComposableCameraPinDirection::Output) { continue; }
				B.Reset();
				B.Append(TEXT("= "));
				ComposableCameraDebug::AppendOutputPinValue(
					B, *DBForOutputs, NodeArrayIdx, Pin.PinName, Pin.PinType, Pin.EnumType);
				// Leading whitespace on the Label places pin lines under the
				// node header's text inset; value column aligns per-node
				// because each node header is a full-line row that closes
				// the preceding KV group.
				Out.Lines.Add({
					FString::Printf(TEXT("         %s"), *Pin.PinName.ToString()),
					FString(B),
					CValue });
			}
		};

		if (Camera->FullExecChain.Num() > 0)
		{
			// Exec-chain path — the canonical order the camera actually ticks in.
			// Count nodes + count SetVariable entries separately for the header.
			int32 NodeStepsInChain = 0;
			int32 SetVarStepsInChain = 0;
			for (const FComposableCameraExecEntry& Entry : Camera->FullExecChain)
			{
				if (Entry.EntryType == EComposableCameraExecEntryType::CameraNode)    { ++NodeStepsInChain; }
				else if (Entry.EntryType == EComposableCameraExecEntryType::SetVariable) { ++SetVarStepsInChain; }
			}
			if (SetVarStepsInChain > 0)
			{
				Out.Lines.Add({ FString::Printf(TEXT("-- Nodes (%d, exec order; +%d var writes) --"),
					NodeStepsInChain, SetVarStepsInChain), CLabel });
			}
			else
			{
				Out.Lines.Add({ FString::Printf(TEXT("-- Nodes (%d, exec order) --"),
					NodeStepsInChain), CLabel });
			}

			int32 StepNum = 0;
			for (const FComposableCameraExecEntry& Entry : Camera->FullExecChain)
			{
				switch (Entry.EntryType)
				{
					case EComposableCameraExecEntryType::CameraNode:
					{
						if (!Camera->CameraNodes.IsValidIndex(Entry.CameraNodeIndex)) { break; }
						const UComposableCameraCameraNodeBase* Node = Camera->CameraNodes[Entry.CameraNodeIndex];
						if (!Node) { break; }
						++StepNum;
						EmitNodeLines(StepNum, Entry.CameraNodeIndex, Node);
						break;
					}
					case EComposableCameraExecEntryType::SetVariable:
					{
						// Subtle, indented line between node steps: shows the
						// variable write that happens right after the previous
						// node's tick. CNeutral keeps it demoted below the
						// colored pin-value lines (the actual current variable
						// value is visible in the Variables section below).
						Out.Lines.Add({
							FString::Printf(TEXT("       -> set %s = <%s>"),
								*Entry.VariableName.ToString(),
								*Entry.SourcePinName.ToString()),
							CNeutral });
						break;
					}
				}
			}
		}
		else
		{
			// Fallback: FullExecChain absent (legacy / non-type-asset camera).
			// Walk CameraNodes linearly — matches the fallback branch in
			// TickCamera exactly.
			int32 NodeCount = 0;
			for (const UComposableCameraCameraNodeBase* Node : Camera->CameraNodes) { if (Node) { ++NodeCount; } }
			Out.Lines.Add({ FString::Printf(TEXT("-- Nodes (%d, array order) --"), NodeCount), CLabel });

			int32 StepNum = 0;
			for (int32 i = 0; i < Camera->CameraNodes.Num(); ++i)
			{
				const UComposableCameraCameraNodeBase* Node = Camera->CameraNodes[i];
				if (!Node) { continue; }
				++StepNum;
				EmitNodeLines(StepNum, i, Node);
			}
		}

		// Exposed parameters + internal variables (read from runtime data block)
		if (TypeAsset && Camera->OwnedRuntimeDataBlock && Camera->OwnedRuntimeDataBlock->IsValid())
		{
			const FComposableCameraRuntimeDataBlock& DB = *Camera->OwnedRuntimeDataBlock;
			const TArray<FComposableCameraExposedParameter>& Params = TypeAsset->GetExposedParameters();
			if (Params.Num() > 0)
			{
				Out.Lines.Add({ FString::Printf(TEXT("-- Parameters (%d) --"), Params.Num()), CLabel });
				TStringBuilder<192> B;
				for (const FComposableCameraExposedParameter& P : Params)
				{
					B.Reset();
					B.Append(TEXT("= "));
					if (const int32* Offset = DB.ExposedParameterOffsets.Find(P.ParameterName))
					{
						ComposableCameraDebug::AppendTypedValue(B, DB, *Offset, P.PinType, P.EnumType);
					}
					else
					{
						B.Append(TEXT("(unresolved)"));
					}
					Out.Lines.Add({
						FString::Printf(TEXT("  %s"), *P.ParameterName.ToString()),
						FString(B),
						CValue });
				}
			}

			if (DB.InternalVariableOffsets.Num() > 0)
			{
				// Build a quick PinType map for formatting (same pattern as DisplayDebug).
				struct FVarTypeInfo { EComposableCameraPinType PinType; const UEnum* EnumType; };
				TMap<FName, FVarTypeInfo> VarTypes;
				VarTypes.Reserve(TypeAsset->InternalVariables.Num() + TypeAsset->ExposedVariables.Num());
				for (const FComposableCameraInternalVariable& Var : TypeAsset->InternalVariables)
				{
					VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
				}
				for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
				{
					VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
				}

				Out.Lines.Add({ FString::Printf(TEXT("-- Variables (%d) --"), DB.InternalVariableOffsets.Num()), CLabel });
				TStringBuilder<192> B;
				for (const auto& Pair : DB.InternalVariableOffsets)
				{
					EComposableCameraPinType PinType = EComposableCameraPinType::Float;
					const UEnum* EnumType = nullptr;
					if (const FVarTypeInfo* Found = VarTypes.Find(Pair.Key))
					{
						PinType = Found->PinType;
						EnumType = Found->EnumType;
					}
					B.Reset();
					B.Append(TEXT("= "));
					ComposableCameraDebug::AppendTypedValue(B, DB, Pair.Value, PinType, EnumType);
					Out.Lines.Add({
						FString::Printf(TEXT("  %s"), *Pair.Key.ToString()),
						FString(B),
						CValue });
				}
			}

			// ---- Data Block stats ----
			// Purely diagnostic view of the runtime data block's layout. Answers
			// "how big is this camera's flat storage" and "which slot dominates"
			// without having to attach a debugger.
			//
			// The data block has TWO storage pools that share the offset map
			// keys: byte `Storage` for POD slots (offset < StructSlotsOffsetBase)
			// and the typed `StructSlots` array for non-POD struct slots
			// (offset >= StructSlotsOffsetBase, indexed by Offset - base). Sizes
			// must be computed differently per pool — POD slot size is the
			// distance to the next consecutive POD offset (with `Storage.Num()`
			// as the upper bound for the last POD slot), struct slot size is
			// the struct's own `GetStructureSize()`. Subtracting a struct-pool
			// synthetic offset from a POD offset (the previous code did this
			// across the boundary) produced a phantom ~1 GB "size" for the
			// last POD slot before the struct pool, hiding which slot was
			// actually the largest.
			//
			// The four sources below are the four that actually occupy slots;
			// InputPinSourceOffsets / ExposedInputPinOffsets are pure wiring
			// tables pointing back into those slots.
			struct FSlotRef
			{
				int32 Offset;
				FComposableCameraPinKey PinKey;   // zero-initialized for non-pin slots
				FName ScalarName;                  // valid for param / var slots
				uint8 Kind;                        // 0=pin, 1=param, 2=var, 3=default
			};
			TArray<FSlotRef, TInlineAllocator<64>> Slots;
			Slots.Reserve(DB.OutputPinOffsets.Num()
						+ DB.ExposedParameterOffsets.Num()
						+ DB.InternalVariableOffsets.Num()
						+ DB.DefaultValueOffsets.Num());
			for (const auto& P : DB.OutputPinOffsets)       { Slots.Add({ P.Value, P.Key,          NAME_None, 0 }); }
			for (const auto& P : DB.ExposedParameterOffsets){ Slots.Add({ P.Value, {},             P.Key,     1 }); }
			for (const auto& P : DB.InternalVariableOffsets){ Slots.Add({ P.Value, {},             P.Key,     2 }); }
			for (const auto& P : DB.DefaultValueOffsets)    { Slots.Add({ P.Value, P.Key,          NAME_None, 3 }); }
			Slots.Sort([](const FSlotRef& A, const FSlotRef& B) { return A.Offset < B.Offset; });

			int32 LargestIdx  = INDEX_NONE;
			int32 LargestSize = 0;
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				const int32 Start = Slots[i].Offset;
				int32 Size = 0;
				if (DB.IsStructSlotOffset(Start))
				{
					// Struct-pool slot — query the typed FInstancedStruct for
					// its actual struct size. Layout-padding context doesn't
					// apply here (each slot owns its own heap allocation).
					const FInstancedStruct& Instance = DB.GetStructSlotChecked(Start);
					if (Instance.IsValid() && Instance.GetScriptStruct())
					{
						Size = Instance.GetScriptStruct()->GetStructureSize();
					}
				}
				else
				{
					// POD slot. Look at the next sorted slot to bound the
					// size, but only if that next slot is also a POD slot —
					// otherwise we're at the boundary between the two pools
					// and must use `Storage.Num()` as the upper bound to
					// avoid the cross-pool phantom-size bug.
					int32 NextBoundary = DB.Storage.Num();
					if (i + 1 < Slots.Num())
					{
						const int32 NextOffset = Slots[i + 1].Offset;
						if (!DB.IsStructSlotOffset(NextOffset))
						{
							NextBoundary = NextOffset;
						}
					}
					Size = NextBoundary - Start;
				}
				if (Size > LargestSize) { LargestSize = Size; LargestIdx = i; }
			}

			Out.Lines.Add({ TEXT("-- Data Block --"), CLabel });
			TStringBuilder<192> DbLine;

			Out.Lines.Add({
				TEXT("  Storage"),
				FString::Printf(TEXT("%d B"), DB.Storage.Num()),
				CValue });

			DbLine.Reset();
			DbLine.Appendf(TEXT("%d"), DB.OutputPinOffsets.Num());
			if (LargestIdx != INDEX_NONE && Slots[LargestIdx].Kind == 0)
			{
				DbLine.Appendf(TEXT("  (largest: node[%d].%s, %d B)"),
					Slots[LargestIdx].PinKey.NodeIndex,
					*Slots[LargestIdx].PinKey.PinName.ToString(),
					LargestSize);
			}
			Out.Lines.Add({ TEXT("  Output pins"), FString(DbLine), CValue });

			Out.Lines.Add({
				TEXT("  Exposed params"),
				FString::Printf(TEXT("%d"), DB.ExposedParameterOffsets.Num()),
				CValue });

			Out.Lines.Add({
				TEXT("  Internal vars"),
				FString::Printf(TEXT("%d"), DB.InternalVariableOffsets.Num()),
				CValue });

			Out.Lines.Add({
				TEXT("  Defaults"),
				FString::Printf(TEXT("%d"), DB.DefaultValueOffsets.Num()),
				CValue });

			// When the largest slot is not an output pin, surface it on a
			// separate line so the "largest" signal isn't lost.
			if (LargestIdx != INDEX_NONE && Slots[LargestIdx].Kind != 0)
			{
				static const TCHAR* const KindPrefix[] = { TEXT(""), TEXT("param"), TEXT("var"), TEXT("default") };
				DbLine.Reset();
				if (Slots[LargestIdx].Kind == 3)
				{
					DbLine.Appendf(TEXT("%s[%d].%s, %d B"),
						KindPrefix[Slots[LargestIdx].Kind],
						Slots[LargestIdx].PinKey.NodeIndex,
						*Slots[LargestIdx].PinKey.PinName.ToString(),
						LargestSize);
				}
				else
				{
					DbLine.Appendf(TEXT("%s.%s, %d B"),
						KindPrefix[Slots[LargestIdx].Kind],
						*Slots[LargestIdx].ScalarName.ToString(),
						LargestSize);
				}
				Out.Lines.Add({ TEXT("  Largest slot"), FString(DbLine), CValue });
			}
		}
	}

	// ---- Region: Actions ----------------------------------------------
	//
	// Three lines per action:
	//   1. <ClassName>  <scope>   — name + camera/persistent
	//   2. exec: <Phase> [-> TargetNode]  — when it fires, and for node-
	//      scoped phases the target node class it runs around
	//   3. expire: <bitmask summary>  — which expiration rules are on,
	//      with ElapsedTime/Duration fraction for the Duration bit
	//
	// Keeps every field public-API accessible: ExecutionType / TargetNodeClass
	// / ExpirationType bits / Duration are all EditAnywhere UPROPERTYs;
	// ElapsedTime is the only thing that needed a getter exposing (added
	// as a BlueprintPure getter on the action base — zero runtime cost,
	// debug-only consumer).
	static const TCHAR* ActionExecToStr(EComposableCameraActionExecutionType Exec)
	{
		switch (Exec)
		{
			case EComposableCameraActionExecutionType::PreCameraTick:  return TEXT("PreCameraTick");
			case EComposableCameraActionExecutionType::PreNodeTick:    return TEXT("PreNodeTick");
			case EComposableCameraActionExecutionType::PostNodeTick:   return TEXT("PostNodeTick");
			case EComposableCameraActionExecutionType::PostCameraTick: return TEXT("PostCameraTick");
		}
		return TEXT("?");
	}

	/** Format the ExpirationType bitmask as a pipe-separated list, with
	 *  per-bit extras (Duration shows ElapsedTime/Duration). Ordering
	 *  matches the enum declaration so unrelated actions read the same
	 *  left-to-right even when they have different active bits. */
	static FString FormatActionExpiration(const UComposableCameraActionBase* Action)
	{
		if (!Action) { return FString(); }
		const uint8 Bits = Action->ExpirationType;

		TArray<FString, TInlineAllocator<4>> Parts;
		if (Bits & static_cast<uint8>(EComposableCameraActionExpirationType::Instant))
		{
			Parts.Add(TEXT("Instant"));
		}
		if (Bits & static_cast<uint8>(EComposableCameraActionExpirationType::Duration))
		{
			Parts.Add(FString::Printf(TEXT("Duration %.2f/%.2fs"),
				Action->GetElapsedTime(), Action->Duration));
		}
		if (Bits & static_cast<uint8>(EComposableCameraActionExpirationType::Manual))
		{
			Parts.Add(TEXT("Manual"));
		}
		if (Bits & static_cast<uint8>(EComposableCameraActionExpirationType::Condition))
		{
			Parts.Add(TEXT("Condition"));
		}

		if (Parts.Num() == 0) { return TEXT("(no expiration bits set)"); }
		return FString::Join(Parts, TEXT(" | "));
	}

	static void BuildActionsLines(const FPanelCtx& Ctx, FRegionLines& Out)
	{
		Out.Title = TEXT("Actions");
		const TSet<UComposableCameraActionBase*>& Actions = Ctx.PCM->CameraActions;

		// Header with count so user sees "(0)" vs "(none)" distinction
		// — makes it clear whether the set is populated at all.
		Out.Lines.Add({ FString::Printf(TEXT("Actions  (%d)"), Actions.Num()), CLabel });
		if (Actions.Num() == 0)
		{
			Out.Lines.Add({ TEXT("  (none)"), CNeutral });
			return;
		}

		for (const UComposableCameraActionBase* Action : Actions)
		{
			if (!Action) { continue; }

			const TCHAR* Scope = Action->bOnlyForCurrentCamera ? TEXT("camera") : TEXT("persist");

			// Line 1: class name + scope tag
			Out.Lines.Add({
				FString::Printf(TEXT("  %s   <%s>"),
					*Action->GetClass()->GetName(), Scope),
				CValue });

			// Line 2: execution phase (+ target node for node-scoped phases)
			FString ExecLine;
			const EComposableCameraActionExecutionType Exec = Action->ExecutionType;
			const bool bNeedsTarget =
				Exec == EComposableCameraActionExecutionType::PreNodeTick ||
				Exec == EComposableCameraActionExecutionType::PostNodeTick;
			if (bNeedsTarget)
			{
				const FString TargetName = Action->TargetNodeClass
					? Action->TargetNodeClass->GetName()
					: TEXT("(null — action will be ignored)");
				ExecLine = FString::Printf(TEXT("    exec:   %s -> %s"),
					ActionExecToStr(Exec), *TargetName);
			}
			else
			{
				ExecLine = FString::Printf(TEXT("    exec:   %s"),
					ActionExecToStr(Exec));
			}
			Out.Lines.Add({ ExecLine, CNeutral });

			// Line 3: expiration summary
			Out.Lines.Add({
				FString::Printf(TEXT("    expire: %s"), *FormatActionExpiration(Action)),
				CNeutral });
		}
	}

	// ---- Region: Modifiers --------------------------------------------
	//
	// Mirrors `BuildModifierDebugString` (PCM) but in structured-lines form
	// so the panel width clipping + coloring applies.
	//
	// Two sub-sections:
	//   1. "Effective (N)": what's actually driving the running camera
	//      right now. One line per node class, showing the winning
	//      modifier (highest priority whose tag matches the camera).
	//   2. "All (M)": every registered modifier grouped by [CameraTag] →
	//      [NodeClass] → modifier entries. The one marked `[*]` inside
	//      each node-class group is the effective winner — makes "why
	//      is my modifier not applying?" trivially answerable (look for
	//      a [*] mark on a different modifier of the same node class).
	//
	// Color convention:
	//   CLabel    — section / group headers
	//   CValue    — modifier body lines
	//   CActiveMarker — effective entry (both in section 1, and `[*]` lines in section 2)
	//   CNeutral  — empty-state "(none)" placeholders
	static void BuildModifiersLines(const FPanelCtx& Ctx, FRegionLines& Out)
	{
		Out.Title = TEXT("Modifiers");

		const UComposableCameraModifierManager* ModMgr = Ctx.PCM->GetModifierManager();
		if (!ModMgr)
		{
			Out.Lines.Add({ TEXT("(no modifier manager)"), CDestroyed });
			return;
		}

		const auto& Data      = ModMgr->GetModifierData();
		const auto& AllMods   = Data.ModifierData;       // TMap<Tag, TMap<NodeClass, TArray<Entry>>>
		const auto& Effective = Data.EffectiveModifiers; // TMap<NodeClass, Entry>

		// ---- Section 1: Effective ----
		Out.Lines.Add({ FString::Printf(TEXT("Effective  (%d)"), Effective.Num()), CLabel });
		if (Effective.Num() == 0)
		{
			Out.Lines.Add({ TEXT("  (none)"), CNeutral });
		}
		else
		{
			for (const auto& Pair : Effective)
			{
				const auto& NodeClass = Pair.Key;
				const auto& Entry     = Pair.Value;

				const FString NodeName = NodeClass
					? NodeClass->GetName()
					: TEXT("(null class)");

				FString ModDesc;
				if (Entry.Modifier && Entry.Asset)
				{
					ModDesc = FString::Printf(TEXT("%s <%s> p=%d"),
						*Entry.Modifier->GetClass()->GetName(),
						*Entry.Asset->GetName(),
						Entry.Asset->Priority);
				}
				else
				{
					ModDesc = TEXT("(destroyed)");
				}

				Out.Lines.Add({
					FString::Printf(TEXT("  %s  <-  %s"), *NodeName, *ModDesc),
					CActiveMarker });
			}
		}

		// ---- Section 2: All, grouped by camera tag ----
		// Count total entries across all (tag, nodeclass) buckets for the header.
		int32 AllCount = 0;
		for (const auto& TagPair : AllMods)
		{
			for (const auto& NodePair : TagPair.Value)
			{
				AllCount += NodePair.Value.Num();
			}
		}
		Out.Lines.Add({ FString::Printf(TEXT("All  (%d)"), AllCount), CLabel });
		if (AllMods.Num() == 0)
		{
			Out.Lines.Add({ TEXT("  (none)"), CNeutral });
			return;
		}

		for (const auto& TagPair : AllMods)
		{
			const FGameplayTag& Tag       = TagPair.Key;
			const auto&         NodeArray = TagPair.Value;

			Out.Lines.Add({
				FString::Printf(TEXT("  [%s]"), *Tag.ToString()),
				CValue });

			for (const auto& NodePair : NodeArray)
			{
				const auto& NodeClass = NodePair.Key;
				const auto& ModList   = NodePair.Value;

				Out.Lines.Add({
					FString::Printf(TEXT("    %s:"),
						NodeClass ? *NodeClass->GetName() : TEXT("(null class)")),
					CLabel });

				// Find the effective modifier for this node class so we
				// can mark the winner with [*] inline. Effective is a flat
				// NodeClass → Entry map (one entry per node class, camera-tag
				// is already factored in by UpdateEffectiveModifiers), so
				// the comparison uses FModifierEntry::operator==.
				const FModifierEntry* EffForNode = Effective.Find(NodeClass);

				for (const FModifierEntry& Entry : ModList)
				{
					if (!Entry.Modifier || !Entry.Asset) { continue; }
					const bool bIsEffective = EffForNode && (*EffForNode) == Entry;
					Out.Lines.Add({
						FString::Printf(TEXT("      %s <%s> p=%d%s"),
							*Entry.Modifier->GetClass()->GetName(),
							*Entry.Asset->GetName(),
							Entry.Asset->Priority,
							bIsEffective ? TEXT("  [*]") : TEXT("")),
						bIsEffective ? CActiveMarker : CValue });
				}
			}
		}
	}

	// ---- Region: Patches ---------------------------------------------
	//
	// Three lines per patch, designed to read at a glance:
	//
	//   ▸ AssetName              L0    Active     a 1.00
	//       7.39 / 10.00 s   active (74%)
	//       expire   Duration · Manual · Condition  +CamChange
	//
	// Visual hierarchy:
	//   Line 1 — phase-colored (cyan/green/amber/red), highest weight, contains
	//            asset identity + key state fields. The line color makes the
	//            patch's lifecycle phase readable in one glance even with many
	//            patches active simultaneously.
	//   Line 2 — neutral (dim), timing data with progress percentage.
	//   Line 3 — neutral, expiration channels spelled out (Duration / Manual /
	//            Condition) rather than D·M·C glyphs — the panel is wide enough
	//            to fit the words, and they read as English instead of cipher.
	//            Skipped entirely when no channels are enabled and OnCameraChange
	//            is off.
	static const FLinearColor CPatchEntering(0.55f, 0.85f, 0.95f, 1.00f); // cyan-ish (matches CLabel)
	static const FLinearColor CPatchActive  (0.45f, 0.95f, 0.55f, 1.00f); // green
	static const FLinearColor CPatchExiting (1.00f, 0.82f, 0.35f, 1.00f); // amber
	static const FLinearColor CPatchExpired (0.75f, 0.45f, 0.45f, 1.00f); // muted red

	static FLinearColor PatchPhaseColor(EComposableCameraPatchPhase Phase)
	{
		switch (Phase)
		{
			case EComposableCameraPatchPhase::Entering: return CPatchEntering;
			case EComposableCameraPatchPhase::Active:   return CPatchActive;
			case EComposableCameraPatchPhase::Exiting:  return CPatchExiting;
			case EComposableCameraPatchPhase::Expired:  return CPatchExpired;
		}
		return CValue;
	}

	static const TCHAR* PatchPhaseLabel(EComposableCameraPatchPhase Phase)
	{
		switch (Phase)
		{
			case EComposableCameraPatchPhase::Entering: return TEXT("Entering");
			case EComposableCameraPatchPhase::Active:   return TEXT("Active  ");
			case EComposableCameraPatchPhase::Exiting:  return TEXT("Exiting ");
			case EComposableCameraPatchPhase::Expired:  return TEXT("Expired ");
		}
		return TEXT("???     ");
	}

	// Emit "X.YY / Z.ZZ s   <action> (NN%)" for the timing line. Action label is
	// chosen by phase: Entering → "enter", Exiting → "exit", Active+Duration →
	// "active", Active+no-Duration → just elapsed. Returns empty if there's no
	// meaningful timing (no duration on Active without channel).
	static FString FormatPatchTimingLine(const FComposableCameraPatchSnapshot& P)
	{
		const EComposableCameraPatchPhase Phase = static_cast<EComposableCameraPatchPhase>(P.Phase);
		auto FormatTiming = [](float Elapsed, float Total, const TCHAR* Label) -> FString
		{
			const float Pct = Total > 0.f ? FMath::Clamp(Elapsed / Total, 0.f, 1.f) * 100.f : 0.f;
			return FString::Printf(TEXT("%.2f / %.2f s   %s (%.0f%%)"),
				Elapsed, Total, Label, Pct);
		};

		if (Phase == EComposableCameraPatchPhase::Entering && P.EnterDuration > 0.f)
		{
			return FormatTiming(P.ElapsedInPhase, P.EnterDuration, TEXT("enter"));
		}
		if (Phase == EComposableCameraPatchPhase::Exiting && P.ExitDuration > 0.f)
		{
			return FormatTiming(P.ElapsedInPhase, P.ExitDuration, TEXT("exit "));
		}
		if (Phase == EComposableCameraPatchPhase::Active
			&& (P.ExpirationType & static_cast<uint8>(EComposableCameraPatchExpirationType::Duration))
			&& P.Duration > 0.f)
		{
			return FormatTiming(P.ElapsedTimeActive, P.Duration, TEXT("active"));
		}
		if (Phase == EComposableCameraPatchPhase::Active)
		{
			return FString::Printf(TEXT("%.2f s   active"), P.ElapsedTimeActive);
		}
		return FString();
	}

	// Spelled-out channel names joined by " · ". Plus suffix " +CamChange" if
	// the auxiliary flag is set. Returns empty when no channel and no flag.
	static FString FormatPatchExpirationLine(const FComposableCameraPatchSnapshot& P)
	{
		TArray<FString, TInlineAllocator<3>> Channels;
		if (P.ExpirationType & static_cast<uint8>(EComposableCameraPatchExpirationType::Duration))  Channels.Add(TEXT("Duration"));
		if (P.ExpirationType & static_cast<uint8>(EComposableCameraPatchExpirationType::Manual))    Channels.Add(TEXT("Manual"));
		if (P.ExpirationType & static_cast<uint8>(EComposableCameraPatchExpirationType::Condition)) Channels.Add(TEXT("Condition"));

		FString Out = FString::Join(Channels, TEXT(" · "));
		if (P.bExpireOnCameraChange)
		{
			if (!Out.IsEmpty()) Out += TEXT("  ");
			Out += TEXT("+CamChange");
		}
		return Out;
	}

	// Row heights / paddings for the structured Patches render.
	static constexpr float KPatchIdentityRowH   = KLineH;   // identity line
	static constexpr float KPatchBarRowH        = 18.f;     // alpha or time bar row
	static constexpr float KPatchBarHeight      = 8.f;      // filled-rect thickness
	static constexpr float KPatchBarTopInset    = 5.f;      // distance from row top to bar top
	static constexpr float KPatchBarIndentPx    = 24.f;     // bar row indent from region left
	static constexpr float KPatchBarLabelW      = 42.f;     // "Alpha" / "Time" / "Expire" label column width
	static constexpr float KPatchBarLabelGap    = 6.f;      // gap between label text and bar
	static constexpr float KPatchBarValueGap    = 8.f;      // gap between bar right edge and numeric value
	static constexpr float KPatchInterRowGap    = 2.f;      // gap between consecutive rows within one patch
	static constexpr float KPatchInterPatchGap  = 6.f;      // gap between adjacent patches

	static bool PatchHasTimeBar(const FComposableCameraPatchSnapshot& P, EComposableCameraPatchPhase Phase)
	{
		if (Phase == EComposableCameraPatchPhase::Entering && P.EnterDuration > 0.f) return true;
		if (Phase == EComposableCameraPatchPhase::Exiting  && P.ExitDuration  > 0.f) return true;
		if (Phase == EComposableCameraPatchPhase::Active
			&& (P.ExpirationType & static_cast<uint8>(EComposableCameraPatchExpirationType::Duration))
			&& P.Duration > 0.f)
		{
			return true;
		}
		return false;
	}

	static bool PatchHasExpire(const FComposableCameraPatchSnapshot& P)
	{
		return P.ExpirationType != 0 || P.bExpireOnCameraChange;
	}

	static float ComputePatchRowHeight(const FComposableCameraPatchSnapshot& P)
	{
		const EComposableCameraPatchPhase Phase = static_cast<EComposableCameraPatchPhase>(P.Phase);
		float H = KPatchIdentityRowH
		        + KPatchInterRowGap + KPatchBarRowH; // identity + Alpha bar always
		if (PatchHasTimeBar(P, Phase)) H += KPatchInterRowGap + KPatchBarRowH;
		if (PatchHasExpire(P))         H += KPatchInterRowGap + KLineH;
		return H;
	}

	static float ComputePatchesBodyHeight(const TArray<FComposableCameraPatchSnapshot>& Snap)
	{
		// Header row "Patches  (N)" always present.
		float H = KLineH;
		if (Snap.Num() == 0)
		{
			// "(none)" placeholder row.
			return H + KLineH;
		}
		for (int32 i = 0; i < Snap.Num(); ++i)
		{
			if (i > 0) H += KPatchInterPatchGap;
			H += ComputePatchRowHeight(Snap[i]);
		}
		return H;
	}

	static void BuildPatchesLines(const FPanelCtx& Ctx, FRegionLines& Out)
	{
		Out.Title      = TEXT("Patches");
		Out.bIsPatches = true;

		// Source 1 — BP path: PCM → ContextStack → ActiveDirector → PatchManager.
		// One snapshot row per patch added via UComposableCameraBlueprintLibrary::AddCameraPatch.
		const UComposableCameraContextStack* Stack = Ctx.PCM->GetContextStack();
		UComposableCameraDirector* Director = Stack ? Stack->GetActiveDirector() : nullptr;
		if (const UComposableCameraPatchManager* Manager = Director ? Director->GetPatchManager() : nullptr)
		{
			Manager->BuildDebugSnapshot(Out.PatchSnapshots);
		}

		// Source 2 — Sequencer path: walk every UComposableCameraLevelSequenceComponent
		// in the world and ask it for its registered overlays. Sequencer-driven
		// patches don't go through PatchManager (they live on the LS Component's
		// SequencerPatchOverlays map and apply directly to the bound CineCamera);
		// merging here is what makes them visible in the debug panel.
		// Iteration cost is cheap — typical scene has 0-2 LS Actors active at
		// once (gated by ECS gate to current cut target + blend partners).
		if (UWorld* World = Ctx.PCM->GetWorld())
		{
			for (TObjectIterator<UComposableCameraLevelSequenceComponent> It; It; ++It)
			{
				UComposableCameraLevelSequenceComponent* LSComp = *It;
				if (!LSComp || !IsValid(LSComp) || LSComp->GetWorld() != World)
				{
					continue;
				}
				LSComp->BuildSequencerPatchSnapshot(Out.PatchSnapshots);
			}
		}

		Out.PatchesBodyHeight = ComputePatchesBodyHeight(Out.PatchSnapshots);
	}

	/**
	 * Render a single progress bar row: "Label  [bar      ] value".
	 *
	 * Layout (left → right, pixel offsets from BarOriginX):
	 *   [0 .. LabelW)                                    — label text
	 *   [LabelW .. BarRight)                             — bar (bg + filled fill + outline)
	 *   [BarRight + gap .. RightX)                       — value text, right-aligned
	 *
	 * The bar's background alpha is 0.15 and the fill alpha is 0.75 — same
	 * ratio the Stack & Tree transition row uses, so the visual weight of
	 * "how much is done" versus "total length" reads consistently.
	 */
	static void DrawPatchBarRow(
		const FPanelCtx& Ctx,
		float BarOriginX, float RowY, float RightX,
		const TCHAR* LabelText, float Progress01,
		const FString& ValueText, const FLinearColor& Hue)
	{
		UCanvas* Canvas = Ctx.Canvas;
		UFont*   Font   = Ctx.BodyFont;

		const float LabelX  = BarOriginX;
		const float BarX    = LabelX + KPatchBarLabelW + KPatchBarLabelGap;
		const float ValueW  = MeasureTextWidth(Canvas, Font, ValueText);
		const float ValueX  = RightX - ValueW;
		const float BarRight = FMath::Max(BarX, ValueX - KPatchBarValueGap);
		const float BarW    = BarRight - BarX;

		// Label (neutral — the bar carries the phase color).
		DrawTextLineClipped(Canvas, Font, LabelText,
			LabelX, RowY + KPatchBarTopInset - 4.f, LabelX + KPatchBarLabelW, CNeutral);

		// Bar body (bg + fill + outline).
		const float BarY = RowY + KPatchBarTopInset;
		const FLinearColor BarBgColor  (Hue.R, Hue.G, Hue.B, 0.15f);
		const FLinearColor BarFillColor(Hue.R, Hue.G, Hue.B, 0.75f);
		const FLinearColor BarBorderColor(Hue.R, Hue.G, Hue.B, 0.35f);
		if (BarW > 0.f)
		{
			DrawFilledRect(Canvas, FVector2D(BarX, BarY), FVector2D(BarW, KPatchBarHeight), BarBgColor);
			const float FillW = BarW * FMath::Clamp(Progress01, 0.f, 1.f);
			if (FillW > 0.f)
			{
				DrawFilledRect(Canvas, FVector2D(BarX, BarY), FVector2D(FillW, KPatchBarHeight), BarFillColor);
			}
			DrawBorder(Canvas, FVector2D(BarX, BarY), FVector2D(BarW, KPatchBarHeight), BarBorderColor, 1.f);
		}

		// Value (numeric) — right-aligned.
		DrawTextLineClipped(Canvas, Font, ValueText,
			ValueX, RowY + KPatchBarTopInset - 4.f, RightX, CNeutral);
	}

	/**
	 * Render the Patches region. For each patch:
	 *   Row A: phase-colored identity text line ("> AssetName  L0  Active").
	 *   Row B: "Alpha  [bar] 1.00" progress bar.
	 *   Row C: "Time   [bar] X.XX / Y.YY s" — only when a meaningful denominator
	 *          exists (Entering/EnterDuration, Exiting/ExitDuration,
	 *          Active/Duration-channel).
	 *   Row D: "Expire  Duration · Manual · Condition  +CamChange" — only when
	 *          at least one channel or the OnCameraChange flag is on.
	 *
	 * The Time bar progress semantic matches the phase's natural direction:
	 *   Entering  → ElapsedInPhase / EnterDuration (fills up as it enters)
	 *   Exiting   → ElapsedInPhase / ExitDuration  (fills up as it exits;
	 *               author reads "how far through the fade-out I am")
	 *   Active    → ElapsedTimeActive / Duration   (fills up toward expiration)
	 */
	static void DrawPatchesStructured(
		const FPanelCtx& Ctx,
		const TArray<FComposableCameraPatchSnapshot>& Snap,
		const FVector2D& BodyPos,
		const FVector2D& BodySize)
	{
		UCanvas* Canvas = Ctx.Canvas;
		UFont*   Font   = Ctx.BodyFont;
		const float RightX = BodyPos.X + BodySize.X;
		float CursorY = BodyPos.Y;

		// Header.
		DrawTextLineClipped(Canvas, Font,
			FString::Printf(TEXT("Patches  (%d)"), Snap.Num()),
			BodyPos.X, CursorY, RightX, CLabel);
		CursorY += KLineH;

		if (Snap.Num() == 0)
		{
			DrawTextLineClipped(Canvas, Font, TEXT("  (none)"), BodyPos.X, CursorY, RightX, CNeutral);
			return;
		}

		for (int32 i = 0; i < Snap.Num(); ++i)
		{
			if (i > 0) CursorY += KPatchInterPatchGap;

			const FComposableCameraPatchSnapshot& P = Snap[i];
			const EComposableCameraPatchPhase Phase = static_cast<EComposableCameraPatchPhase>(P.Phase);
			const FLinearColor Hue = PatchPhaseColor(Phase);

			// Row A — identity.
			// Source-tag prefix lets the designer tell BP-driven patches from
			// Sequencer-driven overlays at a glance. "[Seq] AssetName on Actor"
			// for Sequencer overlays since multiple LS Actors can have overlapping
			// patches and the host actor name disambiguates them; bare AssetName
			// for the BP path (PatchManager / Director-scoped — no host needed).
			FString IdLine;
			if (P.Source == EComposableCameraPatchSource::Sequencer)
			{
				IdLine = FString::Printf(TEXT("  > [Seq] %s on %s        L%-2d   %s"),
					*P.AssetName, *P.HostActorName, P.LayerIndex, PatchPhaseLabel(Phase));
			}
			else
			{
				IdLine = FString::Printf(TEXT("  > %s        L%-2d   %s"),
					*P.AssetName, P.LayerIndex, PatchPhaseLabel(Phase));
			}
			DrawTextLineClipped(Canvas, Font, IdLine, BodyPos.X, CursorY, RightX, Hue);
			CursorY += KPatchIdentityRowH + KPatchInterRowGap;

			// Row B — Alpha bar (always).
			const float BarOriginX = BodyPos.X + KPatchBarIndentPx;
			DrawPatchBarRow(Ctx, BarOriginX, CursorY, RightX,
				TEXT("Alpha"), P.Alpha, FString::Printf(TEXT("%.2f"), P.Alpha), Hue);
			CursorY += KPatchBarRowH + KPatchInterRowGap;

			// Row C — Time bar (conditional).
			if (PatchHasTimeBar(P, Phase))
			{
				float Elapsed = 0.f, Total = 0.f;
				if (Phase == EComposableCameraPatchPhase::Entering)
				{
					Elapsed = P.ElapsedInPhase; Total = P.EnterDuration;
				}
				else if (Phase == EComposableCameraPatchPhase::Exiting)
				{
					Elapsed = P.ElapsedInPhase; Total = P.ExitDuration;
				}
				else // Active + Duration channel
				{
					Elapsed = P.ElapsedTimeActive; Total = P.Duration;
				}
				const float TimeProgress = Total > 0.f
					? FMath::Clamp(Elapsed / Total, 0.f, 1.f) : 0.f;
				DrawPatchBarRow(Ctx, BarOriginX, CursorY, RightX,
					TEXT("Time"), TimeProgress,
					FString::Printf(TEXT("%.2f / %.2f s"), Elapsed, Total), Hue);
				CursorY += KPatchBarRowH + KPatchInterRowGap;
			}

			// Row D — Expire (conditional).
			if (PatchHasExpire(P))
			{
				const FString Line = FString::Printf(TEXT("    Expire   %s"),
					*FormatPatchExpirationLine(P));
				DrawTextLineClipped(Canvas, Font, Line, BodyPos.X, CursorY, RightX, CNeutral);
				CursorY += KLineH;
			}
		}
	}

	// ---- Region: Warnings --------------------------------------------
	//
	// Reads the FComposableCameraLogCapture ring buffer and emits one
	// line per captured entry, newest at the top. Each line is prefixed
	// with an age tag ("Xs ago" / "just now") so the user can tell
	// stale warnings from fresh ones at a glance, and colored by
	// verbosity (Error = red-ish, Warning = amber).
	//
	// Returns false when the buffer is empty AND the caller should skip
	// the region entirely (we don't want an always-on "Warnings (0)"
	// that adds panel height every frame even when nothing is wrong).
	static const FLinearColor CWarnError   (1.00f, 0.40f, 0.35f, 1.00f);
	static const FLinearColor CWarnWarning (1.00f, 0.80f, 0.35f, 1.00f);
	static const FLinearColor CWarnFatal   (1.00f, 0.25f, 0.60f, 1.00f);

	static bool BuildWarningsLines(FRegionLines& Out)
	{
		TArray<FComposableCameraLogEntry> Entries;
		FComposableCameraLogCapture::GetRecentEntries(Entries);
		if (Entries.Num() == 0) { return false; }

		Out.Title = FString::Printf(TEXT("Warnings  (%d)"), Entries.Num());

		const double Now = FPlatformTime::Seconds();

		// Newest first. The ring buffer appends to the end, so we walk
		// backwards.
		for (int32 i = Entries.Num() - 1; i >= 0; --i)
		{
			const FComposableCameraLogEntry& Entry = Entries[i];

			const double AgeSeconds = FMath::Max(0.0, Now - Entry.Timestamp);
			FString AgeStr;
			if (AgeSeconds < 1.0)      { AgeStr = TEXT("just now"); }
			else if (AgeSeconds < 60.0) { AgeStr = FString::Printf(TEXT("%.0fs ago"), AgeSeconds); }
			else                        { AgeStr = FString::Printf(TEXT("%.0fm ago"), AgeSeconds / 60.0); }

			// One-letter verbosity prefix so wide category names don't eat
			// the whole line: F=Fatal, E=Error, W=Warning. The color on
			// the line itself already encodes verbosity; the letter is a
			// compact textual tag for copy/paste.
			const TCHAR* VerbTag = TEXT("?");
			FLinearColor LineColor = CNeutral;
			switch (Entry.Verbosity)
			{
				case ELogVerbosity::Fatal:   VerbTag = TEXT("F"); LineColor = CWarnFatal;   break;
				case ELogVerbosity::Error:   VerbTag = TEXT("E"); LineColor = CWarnError;   break;
				case ELogVerbosity::Warning: VerbTag = TEXT("W"); LineColor = CWarnWarning; break;
				default: break;
			}

			// Strip the "LogComposableCamera" prefix to save horizontal
			// space — every line has the same prefix, so the remaining
			// suffix (`System` / `SystemEditor`) is what's distinguishing.
			FString CatShort = Entry.CategoryName.ToString();
			if (CatShort.StartsWith(TEXT("LogComposableCamera")))
			{
				CatShort.RightChopInline(FCString::Strlen(TEXT("LogComposableCamera")));
			}

			// Compose. Message intentionally last so truncation eats the
			// least important part (the prefix tags are almost always
			// needed; if the message is long it'll get "..."-clipped).
			FString DisplayMessage = Entry.Message;
			if (Entry.RepeatCount > 1)
			{
				DisplayMessage += FString::Printf(TEXT("  (x%d)"), Entry.RepeatCount);
			}
			Out.Lines.Add({
				FString::Printf(TEXT("[%s] %-8s %s : %s"),
					VerbTag, *AgeStr, *CatShort, *DisplayMessage),
				LineColor });
		}

		return true;
	}

	// ---- Region: Legend -----------------------------------------------
	//
	// Matches screen colors to node/transition names. Populated from a
	// static table of (label, color, CVar-name, is-transition) tuples —
	// entries whose CVar is zero (AND the corresponding `.All` CVar is
	// zero) are filtered out, so the legend shrinks to the set of gizmos
	// the user has actually enabled.
	//
	// Colors duplicated from each per-node / per-transition draw site.
	// When a new gizmo is added elsewhere, add its entry here too.
	// (No central color registry yet — if one gets built later, this
	// table is the natural consumer.)
	struct FLegendEntry
	{
		const TCHAR*  Label;
		FLinearColor  Color;
		const TCHAR*  CVarName;
		bool          bIsTransition;
	};

	static const FLegendEntry KLegendEntries[] = {
		// Transitions — accent color used for both the progress sphere and
		// the path polyline. See TechDoc.md §3.20.4 "Accent-color reservation policy".
		{ TEXT("Linear"),             FLinearColor(200.f / 255.f, 200.f / 255.f, 200.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.Linear"),             true },
		{ TEXT("Smooth"),             FLinearColor(255.f / 255.f, 220.f / 255.f, 100.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.Smooth"),             true },
		{ TEXT("Ease"),               FLinearColor(255.f / 255.f, 160.f / 255.f,  80.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.Ease"),               true },
		{ TEXT("Cubic"),              FLinearColor(180.f / 255.f, 130.f / 255.f, 255.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.Cubic"),              true },
		{ TEXT("Inertialized"),       FLinearColor(255.f / 255.f, 100.f / 255.f, 200.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.Inertialized"),       true },
		{ TEXT("Cylindrical"),        FLinearColor(100.f / 255.f, 230.f / 255.f, 200.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.Cylindrical"),        true },
		{ TEXT("Spline (trans.)"),    FLinearColor(140.f / 255.f, 200.f / 255.f, 255.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.Spline"),             true },
		{ TEXT("PathGuided"),         FLinearColor(255.f / 255.f, 130.f / 255.f, 130.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.PathGuided"),         true },
		{ TEXT("DynamicDeocclusion"), FLinearColor(255.f / 255.f,  90.f / 255.f,  90.f / 255.f, 1.f), TEXT("CCS.Debug.Viewport.Transitions.DynamicDeocclusion"), true },

		// Nodes — colors taken from each node's DrawNodeDebug override.
		{ TEXT("PivotOffset"),          FLinearColor(1.f,        1.f,        0.f,        1.f), TEXT("CCS.Debug.Viewport.PivotOffset"),          false },  // Yellow
		{ TEXT("PivotDamping"),         FLinearColor(1.f,        0.f,        1.f,        1.f), TEXT("CCS.Debug.Viewport.PivotDamping"),         false },  // Magenta
		{ TEXT("LookAt"),               FLinearColor(0.f,        1.f,        1.f,        1.f), TEXT("CCS.Debug.Viewport.LookAt"),               false },  // Cyan
		{ TEXT("CollisionPush"),        FLinearColor(0.3f,       0.85f,      0.3f,       1.f), TEXT("CCS.Debug.Viewport.CollisionPush"),        false },  // Green (clear state; red when blocked)
		{ TEXT("OcclusionFade"),        FLinearColor(255.f/255.f, 80.f/255.f, 80.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.OcclusionFade"),        false },  // Red (sweep endpoint + F8-only sweep line; cyan proximity sphere uses a secondary hue)
		{ TEXT("VolumeConstraint"),     FLinearColor(90.f/255.f, 255.f/255.f,120.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.VolumeConstraint"),     false },  // Green (clear state; red when clamping)
		{ TEXT("FocusPull"),            FLinearColor(255.f/255.f,200.f/255.f, 60.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.FocusPull"),            false },  // Amber (target sphere + focus plane)
		{ TEXT("HitchcockZoom"),        FLinearColor(180.f/255.f, 80.f/255.f,220.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.HitchcockZoom"),        false },  // Purple (target + camera spheres + F8 frustum)
		{ TEXT("Spline (node)"),        FLinearColor(180.f/255.f,120.f/255.f,255.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.Spline"),               false },  // Violet
		{ TEXT("Spiral"),               FLinearColor(255.f/255.f,150.f/255.f, 60.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.Spiral"),               false },  // Orange
		{ TEXT("ReceivePivotActor"),    FLinearColor(1.f,        1.f,        1.f,        1.f), TEXT("CCS.Debug.Viewport.ReceivePivotActor"),    false },  // White
		{ TEXT("RelativeFixedPose"),    FLinearColor(1.f,        0.55f,      0.1f,       1.f), TEXT("CCS.Debug.Viewport.RelativeFixedPose"),    false },  // Orange
		{ TEXT("ScreenSpacePivot"),     FLinearColor(80.f/255.f, 200.f/255.f,180.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.ScreenSpacePivot"),     false },  // Teal
		{ TEXT("ScreenSpaceConstr."),   FLinearColor(255.f/255.f,180.f/255.f,220.f/255.f, 1.f), TEXT("CCS.Debug.Viewport.ScreenSpaceConstraints"),false },  // Pink
	};

	// Universal transition endpoint markers — source/target colors painted
	// by DrawStandardTransitionDebug. Only relevant when at least one
	// transition CVar is on, so we gate these on ShouldShowAllTransitionGizmos()
	// OR any per-transition entry being enabled.
	static const FLinearColor CLegendSource  (80.f / 255.f, 220.f / 255.f, 120.f / 255.f, 1.f);
	static const FLinearColor CLegendTarget  (80.f / 255.f, 170.f / 255.f, 255.f / 255.f, 1.f);

	/** Read an int32 CVar by name. Returns false if the CVar doesn't exist
	 *  or is zero. String lookup is done fresh each call — fine at legend
	 *  frequency (at most ~20 lookups per frame). */
	static bool IsCVarEnabled(const TCHAR* Name)
	{
		if (!Name) { return false; }
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar && CVar->GetInt() != 0;
	}

	/** True when at least one transition gizmo is currently drawing — used
	 *  to decide whether to show the universal Source/Target swatches. */
	static bool AnyTransitionEnabled()
	{
		if (FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos())
		{
			return true;
		}
		for (const FLegendEntry& E : KLegendEntries)
		{
			if (E.bIsTransition && IsCVarEnabled(E.CVarName))
			{
				return true;
			}
		}
		return false;
	}

	/** Populate a list of legend entries to actually show this frame.
	 *  Two parallel arrays are built — one per column (transitions on
	 *  the left, nodes on the right). Emptiness of both columns means
	 *  the region is hidden entirely by the height-pass.
	 *
	 *  Legend is only useful when viewport debug is actually drawing
	 *  colors — if the master `CCS.Debug.Viewport` gate is off, every
	 *  per-item CVar is inert and we'd end up labelling colors that
	 *  aren't on screen. Early-return empty in that case. */
	static void BuildLegendRows(
		TArray<TPair<FString, FLinearColor>>& OutTransitionRows,
		TArray<TPair<FString, FLinearColor>>& OutNodeRows)
	{
		if (!IsCVarEnabled(TEXT("CCS.Debug.Viewport")))
		{
			return;
		}

		const bool bShowAllTransitions = FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos();
		const bool bShowAllNodes       = FComposableCameraViewportDebug::ShouldShowAllNodeGizmos();

		// Universal entries first (both lead the transition column).
		if (AnyTransitionEnabled())
		{
			OutTransitionRows.Add({ TEXT("Source pose"), CLegendSource });
			OutTransitionRows.Add({ TEXT("Target pose"), CLegendTarget });
		}

		for (const FLegendEntry& E : KLegendEntries)
		{
			const bool bEnabled = E.bIsTransition
				? (bShowAllTransitions || IsCVarEnabled(E.CVarName))
				: (bShowAllNodes       || IsCVarEnabled(E.CVarName));
			if (!bEnabled) { continue; }

			auto& TargetList = E.bIsTransition ? OutTransitionRows : OutNodeRows;
			TargetList.Add({ E.Label, E.Color });
		}
	}

	/** Number of rows the Nodes section occupies when split across two
	 *  sub-columns. First sub-column takes the ceiling half so odd counts
	 *  land with an extra entry on the left, matching typical top-to-bottom
	 *  reading order. */
	static int32 NodeSubColumnRows(int32 TotalNodeRows)
	{
		return (TotalNodeRows + 1) / 2;
	}

	/** Height (in px) the legend region would occupy this frame. Zero if
	 *  both columns are empty — used to skip the region entirely. */
	static float ComputeLegendBodyHeight(
		const TArray<TPair<FString, FLinearColor>>& TransRows,
		const TArray<TPair<FString, FLinearColor>>& NodeRows)
	{
		const int32 Rows = FMath::Max(TransRows.Num(), NodeSubColumnRows(NodeRows.Num()));
		if (Rows == 0) { return 0.f; }

		// One sub-header ("Transitions" / "Nodes") + N rows. All columns
		// share a common header line at the top, giving +1 line.
		return KLineH * (Rows + 1);
	}

	/** Render the legend in three columns: transitions on the left, then the
	 *  node list flowed into two sub-columns on the right (top-to-bottom,
	 *  left-to-right). Each row: 10x10 filled rect + 4px gap + label text.
	 *
	 *  The node list outgrew a single column once the per-node gizmo count
	 *  crossed ~10; splitting keeps the legend body at roughly the same
	 *  height as the transition list instead of forcing the panel to grow
	 *  vertically just to show all node entries. */
	static void DrawLegendStructured(
		const FPanelCtx& Ctx,
		const TArray<TPair<FString, FLinearColor>>& TransRows,
		const TArray<TPair<FString, FLinearColor>>& NodeRows,
		const FVector2D& BodyPos,
		const FVector2D& BodySize)
	{
		UCanvas* Canvas = Ctx.Canvas;
		UFont*   Font   = Ctx.BodyFont;

		constexpr float KSwatchSize = 10.f;
		constexpr float KSwatchGap  = 4.f;

		// Three-column layout: transitions | nodes-A | nodes-B
		// Two inter-column gaps of KPadding. Negative ColumnW is clamped
		// to 0 so an absurdly narrow panel degrades gracefully.
		const float ColumnW = FMath::Max(0.f, (BodySize.X - 2.f * KPadding) / 3.f);
		const float Col0X   = BodyPos.X;
		const float Col1X   = BodyPos.X + ColumnW + KPadding;
		const float Col2X   = BodyPos.X + 2.f * (ColumnW + KPadding);

		// Column headers — skip for empty columns so the layout doesn't
		// show an orphan "Transitions" / "Nodes" label above a blank list.
		float Y = BodyPos.Y;
		if (TransRows.Num() > 0)
		{
			DrawTextLineClipped(Canvas, Font, TEXT("Transitions"),
				Col0X, Y, Col0X + ColumnW, CLabel);
		}
		if (NodeRows.Num() > 0)
		{
			// Single "Nodes" header above the first sub-column. The second
			// sub-column reads as a visual continuation; duplicating the
			// header would just add noise.
			DrawTextLineClipped(Canvas, Font, TEXT("Nodes"),
				Col1X, Y, Col1X + ColumnW, CLabel);
		}
		Y += KLineH;

		auto DrawColumn = [&](const TArray<TPair<FString, FLinearColor>>& Rows,
		                      int32 StartIdx, int32 EndExclusive, float ColX)
		{
			float RowY = Y;
			for (int32 i = StartIdx; i < EndExclusive; ++i)
			{
				const TPair<FString, FLinearColor>& Row = Rows[i];

				// Color swatch aligned to the text baseline — vertically
				// centered in the line by shifting down by ~1.5px.
				DrawFilledRect(Canvas,
					FVector2D(ColX, RowY + (KLineH - KSwatchSize) * 0.5f),
					FVector2D(KSwatchSize, KSwatchSize),
					Row.Value);

				DrawTextLineClipped(Canvas, Font, Row.Key,
					ColX + KSwatchSize + KSwatchGap, RowY,
					ColX + ColumnW, CValue);

				RowY += KLineH;
			}
		};

		DrawColumn(TransRows, 0, TransRows.Num(), Col0X);

		const int32 NodeSplit = NodeSubColumnRows(NodeRows.Num());
		DrawColumn(NodeRows, 0,         NodeSplit,       Col1X);
		DrawColumn(NodeRows, NodeSplit, NodeRows.Num(), Col2X);
	}

	// ---- Structured render for the Current Pose region ---------------
	//
	// Splits the body area in two equal columns separated by KPadding, then
	// walks `R.PoseGroups`: indices [0..PoseLeftGroupCount) render into the
	// left column, the rest into the right. Each group emits `-- Header --`
	// in CLabel followed by its KV lines; the Value-column X is computed
	// per-group from the widest label in that group so values align cleanly
	// within a group without pushing short-label groups' values far right.
	//
	// Rows are line-clipped vertically against the column's bottom edge so
	// a too-tall group (e.g. a vertically-cramped viewport) silently drops
	// trailing lines rather than overdrawing neighbouring regions. The
	// height estimator in PaintPanel sizes the region to the tallest
	// column, so under normal conditions every row has room.
	static void DrawPoseStructured(
		const FPanelCtx& Ctx,
		const FRegionLines& R,
		const FVector2D& BodyPos,
		const FVector2D& BodySize)
	{
		UCanvas* Canvas = Ctx.Canvas;
		UFont*   Font   = Ctx.BodyFont;

		constexpr float KColumnGap     = KPadding;
		constexpr float KLabelValueGap = 10.f;

		const float ColumnW = FMath::Max(0.f, (BodySize.X - KColumnGap) * 0.5f);
		const float LeftX   = BodyPos.X;
		const float RightX  = BodyPos.X + ColumnW + KColumnGap;
		const float MaxY    = BodyPos.Y + BodySize.Y;

		auto DrawColumn = [&](float ColX, int32 StartIdx, int32 EndExclusive)
		{
			const float ColRightX = ColX + ColumnW;
			float Y = BodyPos.Y;

			for (int32 GI = StartIdx; GI < EndExclusive; ++GI)
			{
				const FPoseGroup& G = R.PoseGroups[GI];

				// Group header — matches the `-- Section --` style used by
				// the Running Camera region for visual consistency.
				if (Y + KLineH > MaxY) { return; }
				const FString Header = FString::Printf(TEXT("-- %s --"), *G.Header);
				DrawTextLineClipped(Canvas, Font, Header, ColX, Y, ColRightX, CLabel);
				Y += KLineH;

				// Per-group label alignment. Proportional font ⇒ measure
				// the widest label in this group so all values in the group
				// line up at the same X.
				float MaxLabelPx = 0.f;
				for (const FPanelLine& L : G.Lines)
				{
					MaxLabelPx = FMath::Max(MaxLabelPx,
						MeasureTextWidth(Canvas, Font, L.Label));
				}
				const float ValueX = ColX + MaxLabelPx + KLabelValueGap;

				for (const FPanelLine& L : G.Lines)
				{
					if (Y + KLineH > MaxY) { return; }
					DrawTextLineClipped(Canvas, Font, L.Label,
						ColX, Y, ValueX, L.Color);
					DrawTextLineClipped(Canvas, Font, L.Value,
						ValueX, Y, ColRightX, L.Color);
					Y += KLineH;
				}
			}
		};

		DrawColumn(LeftX,  0,                          R.PoseLeftGroupCount);
		DrawColumn(RightX, R.PoseLeftGroupCount,       R.PoseGroups.Num());
	}

	// Find a ComposableCamera PCM. `UDebugDrawService::Draw`'s PC parameter is
	// nullptr on some call paths in UE 5.6 (the World-less `Draw(Flags, Canvas)`
	// overload, and some editor-PIE timings), so we fall back to scanning world
	// contexts for a PIE/Game world and grabbing its first local PC.
	static AComposableCameraPlayerCameraManager* ResolvePCM(APlayerController* DelegatePC)
	{
		if (DelegatePC)
		{
			if (auto* PCM = Cast<AComposableCameraPlayerCameraManager>(DelegatePC->PlayerCameraManager))
			{
				return PCM;
			}
		}
		if (!GEngine) { return nullptr; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* World = Ctx.World();
			if (!World) { continue; }
			if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) { continue; }
			if (APlayerController* LocalPC = World->GetFirstPlayerController())
			{
				if (auto* PCM = Cast<AComposableCameraPlayerCameraManager>(LocalPC->PlayerCameraManager))
				{
					return PCM;
				}
			}
		}
		return nullptr;
	}

	// True if we're currently running a world where a PCM is expected to
	// exist — PIE (incl. F8-ejected PIE, which keeps the PIE world alive)
	// or a standalone Game world. Returns false in editor-idle state, so
	// the "no PCM found" banner in the main panel can stay silent when
	// the delegate fires via the `"Editor"` debug-draw channel while PIE
	// isn't running. Before dual-channel registration, this was never
	// ambiguous (the `"Game"` channel only fired when a game world was
	// active); adding the `"Editor"` channel made the distinction matter.
	static bool HasPIEOrGameWorld()
	{
		if (!GEngine) { return false; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
			{
				if (Ctx.World())
				{
					return true;
				}
			}
		}
		return false;
	}

	// ─────────────────────────────────────────────────────────────────
	// Pose History panel (right-side, independent of main panel).
	//
	// 6 sparkline rows stacked vertically: Pos.X / Pos.Y / Pos.Z / Rot.P
	// / Rot.Y / Rot.R. Time axis is shared across all rows (left = oldest
	// of the ~2-second ring buffer, right = current frame). Each row
	// auto-normalizes its y-axis to its own min/max over the visible
	// window, so a 1°-yaw jitter and a 500-unit position spike both look
	// equally tall — sparklines are about SHAPE, not absolute scale.
	//
	// Context-change markers: whenever two adjacent entries have
	// different ContextName, a thin vertical line crosses all six rows
	// at that position — helps correlate pose spikes with context pushes.
	//
	// Mouse hover: when the mouse is inside the sparkline block, draw a
	// vertical cursor at the hovered frame's X position + a tooltip
	// block to the right (or left, if it would clip) showing the pose
	// values at that frame. No cursor when mouse is outside — default
	// state is static sparklines only.
	// ─────────────────────────────────────────────────────────────────
	static constexpr float KPoseHistoryRowH       = 22.f; // per-sparkline row height
	static constexpr float KPoseHistoryLabelW     = 52.f; // left column for "Pos.X" / "Rot.Y" label
	static constexpr float KPoseHistoryValueW     = 60.f; // right column for current numeric value
	static constexpr int32 KPoseHistoryNumRows    = 6;

	static const FLinearColor CPoseHistoryLine    (0.95f, 0.88f, 0.55f, 0.95f); // warm curve
	static const FLinearColor CPoseHistoryBaseline(0.55f, 0.50f, 0.65f, 0.35f); // zero-line underlay
	static const FLinearColor CPoseHistoryContext (0.60f, 1.00f, 0.90f, 0.60f); // context-switch marker
	static const FLinearColor CPoseHistoryCursor  (1.00f, 0.60f, 0.20f, 0.95f); // hover cursor

	/** One sparkline row descriptor — resolves an entry to the scalar
	 *  value that row tracks, plus knows how to format that scalar as a
	 *  display string. `Format` is a `TFunction` (not a printf-style
	 *  format string) because UE 5.6's `FString::Printf` requires a
	 *  `consteval` format literal — runtime `const TCHAR*` variables are
	 *  rejected at compile time. Keeping the format call inside each
	 *  lambda keeps the literal visible to the compile-time check while
	 *  still letting per-row specs pick different widths (Position rows
	 *  use `%8.1f`, Rotation rows use `%+7.1f`). */
	struct FSparklineSpec
	{
		const TCHAR* Label;
		TFunction<float(const FComposableCameraPoseHistoryEntry&)> Extract;
		TFunction<FString(float)> Format;
	};

	/** Draw one sparkline row into a pre-computed rect.
	 *  @param RowRect     Outer bounds of the row (label + plot + value).
	 *  @param PlotLeft    Left X of the plot area.
	 *  @param PlotRight   Right X of the plot area.
	 *  @param History     Chronological history, oldest first.
	 *  @param Spec        Which scalar this row visualises.
	 *  @param HoverIdxOrNeg1  History index the mouse is over, or -1. */
	static void DrawSparklineRow(
		UCanvas* Canvas,
		UFont* BodyFont,
		const FVector2D& RowPos,
		float RowW,
		float PlotLeft,
		float PlotRight,
		const TArray<FComposableCameraPoseHistoryEntry>& History,
		const FSparklineSpec& Spec,
		int32 HoverIdxOrNeg1)
	{
		const float RowY    = RowPos.Y;
		const float PlotW   = FMath::Max(2.f, PlotRight - PlotLeft);
		const float PlotTop = RowY + 2.f;
		const float PlotBot = RowY + KPoseHistoryRowH - 2.f;
		const float PlotH   = FMath::Max(4.f, PlotBot - PlotTop);

		// Label column.
		DrawTextLineClipped(Canvas, BodyFont, Spec.Label,
			RowPos.X + 4.f, RowY + 4.f,
			RowPos.X + KPoseHistoryLabelW - 4.f, CLabel);

		if (History.Num() < 2)
		{
			// Not enough samples to draw a line — just skip the plot,
			// keep the label visible so the layout doesn't jump on
			// startup while the ring warms up.
			return;
		}

		// Compute min/max over the history for this row's scalar.
		float MinV = FLT_MAX, MaxV = -FLT_MAX;
		for (const FComposableCameraPoseHistoryEntry& E : History)
		{
			const float V = Spec.Extract(E);
			MinV = FMath::Min(MinV, V);
			MaxV = FMath::Max(MaxV, V);
		}
		// Guard against flat-line rows — avoid zero range so we don't
		// divide by zero; if all samples are equal, show a centered line.
		float Range = MaxV - MinV;
		if (Range < 1e-5f)
		{
			Range = 1.f;
			MinV -= 0.5f;
		}

		// Baseline (min-value horizontal line) for context — looks like
		// a faint floor under the curve.
		DrawFilledRect(Canvas,
			FVector2D(PlotLeft, PlotBot - 1.f),
			FVector2D(PlotW, 1.f),
			CPoseHistoryBaseline);

		// Draw the polyline.
		const int32 N = History.Num();
		auto MapToXY = [&](int32 Idx) -> FVector2D
		{
			const float NormX = static_cast<float>(Idx) / static_cast<float>(N - 1);
			const float V     = Spec.Extract(History[Idx]);
			const float NormY = (V - MinV) / Range;  // 0..1 (0 = MinV at bottom)
			return FVector2D(PlotLeft + NormX * PlotW, PlotBot - NormY * PlotH);
		};

		// Two-pass fake AA: wider translucent halo + crisp opaque core.
		// FCanvasLineItem has no native AA, stacking the halo hides stair-stepping.
		const FLinearColor HaloColor =
			CPoseHistoryLine.CopyWithNewOpacity(CPoseHistoryLine.A * 0.35f);

		FVector2D Prev = MapToXY(0);
		for (int32 i = 1; i < N; ++i)
		{
			const FVector2D Cur = MapToXY(i);

			FCanvasLineItem Halo(Prev, Cur);
			Halo.SetColor(HaloColor);
			Halo.LineThickness = 2.6f;
			Canvas->DrawItem(Halo);

			FCanvasLineItem Core(Prev, Cur);
			Core.SetColor(CPoseHistoryLine);
			Core.LineThickness = 1.0f;
			Canvas->DrawItem(Core);

			Prev = Cur;
		}

		// Context-switch markers — thin vertical lines wherever adjacent
		// entries' ContextName differs. Drawn in muted teal so they don't
		// compete with the curve.
		for (int32 i = 1; i < N; ++i)
		{
			if (History[i].ContextName != History[i - 1].ContextName)
			{
				const float NormX = static_cast<float>(i) / static_cast<float>(N - 1);
				const float MarkerX = PlotLeft + NormX * PlotW;
				DrawFilledRect(Canvas,
					FVector2D(MarkerX, PlotTop),
					FVector2D(1.f, PlotH),
					CPoseHistoryContext);
			}
		}

		// Hover cursor: a vertical line at the hovered frame. Tooltip is
		// drawn once outside this row (across all 6 rows); this function
		// only draws the cursor portion that lives in THIS row.
		if (HoverIdxOrNeg1 >= 0 && HoverIdxOrNeg1 < N)
		{
			const float NormX = static_cast<float>(HoverIdxOrNeg1) / static_cast<float>(N - 1);
			const float CursorX = PlotLeft + NormX * PlotW;
			DrawFilledRect(Canvas,
				FVector2D(CursorX, PlotTop),
				FVector2D(1.f, PlotH),
				CPoseHistoryCursor);
		}

		// Right-column current numeric value — read from the newest
		// entry (or the hovered one if scrubbing).
		const FComposableCameraPoseHistoryEntry& DisplayEntry =
			(HoverIdxOrNeg1 >= 0 && HoverIdxOrNeg1 < N)
			? History[HoverIdxOrNeg1]
			: History.Last();
		const FString ValueStr = Spec.Format(Spec.Extract(DisplayEntry));
		DrawTextLineClipped(Canvas, BodyFont, ValueStr,
			PlotRight + 4.f, RowY + 4.f,
			RowPos.X + RowW - 4.f, CValue);
	}

	/**
	 * Resolve the mouse cursor's position in `Canvas`-local pixel coords,
	 * or return false if there's no meaningful answer.
	 *
	 * Why not `PlayerController::GetMousePosition`:
	 *   `UDebugDrawService` broadcasts delegates with PC = nullptr
	 *   (engine hardcodes it in `MulticastDelegate.Broadcast(Canvas, nullptr)`).
	 *   The PC path can still be tried via the PCM's owning controller,
	 *   but in game mode with the cursor captured, `GetMousePosition`
	 *   returns false even though the OS cursor has a position. We need
	 *   a path that works whether the cursor is captured or released.
	 *
	 * Strategy:
	 *   1. If Slate is initialized AND the game viewport widget has a
	 *      valid cached geometry, use `FSlateApplication::GetCursorPos()`
	 *      (absolute screen coords) and `Geom.AbsoluteToLocal` (handles
	 *      DPI + window chrome offset). This works in PIE both possessed
	 *      and F8-ejected.
	 *   2. Scale the widget-local position by `Canvas->SizeX / WidgetSize`
	 *      to get Canvas-pixel coords (Canvas pixel space and widget-
	 *      local space differ by the DPI / rendering scale factor).
	 *   3. Reject positions outside the widget bounds — a cursor that
	 *      left the viewport shouldn't show as "hovering frame N".
	 */
	static bool ResolveMouseCanvasPos(UCanvas* Canvas, float& OutX, float& OutY)
	{
		if (!Canvas || !FSlateApplication::IsInitialized()) { return false; }

		UGameViewportClient* GVC = GEngine ? GEngine->GameViewport : nullptr;
		if (!GVC) { return false; }

		TSharedPtr<SViewport> ViewportWidget = GVC->GetGameViewportWidget();
		if (!ViewportWidget.IsValid()) { return false; }

		const FGeometry Geom = ViewportWidget->GetCachedGeometry();
		const FVector2D WidgetSize = Geom.GetLocalSize();
		if (WidgetSize.X <= 0.f || WidgetSize.Y <= 0.f) { return false; }

		const FVector2D ScreenPos = FSlateApplication::Get().GetCursorPos();
		const FVector2D LocalPos  = Geom.AbsoluteToLocal(ScreenPos);

		if (LocalPos.X < 0.f || LocalPos.Y < 0.f ||
			LocalPos.X > WidgetSize.X || LocalPos.Y > WidgetSize.Y)
		{
			return false;
		}

		// Widget-local is in pre-render-scale pixels; Canvas->SizeX is the
		// backbuffer pixel dimension. They match exactly at DPI=1 and
		// scale=1, and differ proportionally otherwise. Compute the ratio
		// rather than assume.
		const double ScaleX = static_cast<double>(Canvas->SizeX) / static_cast<double>(WidgetSize.X);
		const double ScaleY = static_cast<double>(Canvas->SizeY) / static_cast<double>(WidgetSize.Y);
		OutX = static_cast<float>(LocalPos.X * ScaleX);
		OutY = static_cast<float>(LocalPos.Y * ScaleY);
		return true;
	}

	static void DrawPoseHistoryPanel(UCanvas* Canvas, APlayerController* PC)
	{
		if (CVarPoseHistoryEnabled.GetValueOnGameThread() == 0) { return; }
		if (!Canvas || Canvas->SizeX <= 0 || Canvas->SizeY <= 0) { return; }

		AComposableCameraPlayerCameraManager* PCM = ResolvePCM(PC);
		if (!PCM) { return; }

		TArray<FComposableCameraPoseHistoryEntry> History;
		PCM->GetPoseHistory(History);

		UFont* HeaderFont = GEngine ? GEngine->GetMediumFont() : nullptr;
		UFont* BodyFont   = GEngine ? GEngine->GetSmallFont()  : nullptr;
		if (!HeaderFont || !BodyFont) { return; }

		// Layout: right-anchored panel. Width from CVar, height from row
		// count + title + padding. Panel sits mirrored from the main one.
		const float ScreenW = static_cast<float>(Canvas->SizeX);
		const float PanelW  = FMath::Clamp(CVarPoseHistoryWidth.GetValueOnGameThread(), 0.15f, 0.50f) * ScreenW;
		const float PanelH  = KTitleBarH + KPadding * 2.f + KPoseHistoryRowH * KPoseHistoryNumRows;
		const float PanelX  = ScreenW - PanelW - KMargin;
		const float PanelY  = KMargin;

		// Panel backdrop + border (same visual vocabulary as the main panel).
		DrawFilledRect(Canvas, FVector2D(PanelX, PanelY), FVector2D(PanelW, PanelH), CPanelBG);
		DrawBorder    (Canvas, FVector2D(PanelX, PanelY), FVector2D(PanelW, PanelH), CBorder, 1.5f);

		// Title bar.
		DrawFilledRect(Canvas,
			FVector2D(PanelX + KPadding, PanelY + KPadding),
			FVector2D(PanelW - 2.f * KPadding, KTitleBarH), CTitleBG);

		// Header text shows the elapsed window (e.g. "Pose History (1.87s, 112 frames)")
		// so users know the sparklines' time span at a glance. When the
		// `CCS.Debug.Panel.PoseHistory.Freeze` CVar is on, append a
		// `[FROZEN]` tag so the reader immediately understands why the
		// sparklines aren't scrolling.
		const bool bFrozen = AComposableCameraPlayerCameraManager::IsPoseHistoryFrozen();
		FString TitleStr = TEXT("Pose History");
		if (History.Num() >= 2)
		{
			const float Span = History.Last().GameTime - History[0].GameTime;
			TitleStr = FString::Printf(TEXT("Pose History  (%.2fs, %d frames)"), Span, History.Num());
		}
		if (bFrozen)
		{
			TitleStr += TEXT("  [FROZEN]");
		}
		// Frozen title renders in the same accent the pose-history cursor
		// uses (warm orange) so the frozen state visually matches the
		// vertical cursor line the user gets while hovering — makes the
		// "this panel is holding its contents" affordance obvious.
		DrawTextLineClipped(Canvas, HeaderFont, TitleStr,
			PanelX + KPadding + 4.f, PanelY + KPadding + 2.f,
			PanelX + PanelW - KPadding, bFrozen ? CPoseHistoryCursor : CTitle);

		// Compute plot-area bounds once; shared across all 6 rows.
		const float RowsLeft   = PanelX + KPadding;
		const float RowsRight  = PanelX + PanelW - KPadding;
		const float PlotLeft   = RowsLeft + KPoseHistoryLabelW;
		const float PlotRight  = RowsRight - KPoseHistoryValueW;
		const float RowW       = RowsRight - RowsLeft;
		const float RowsTopY   = PanelY + KPadding + KTitleBarH;

		// Resolve mouse-hover frame index. Uses a Slate-based fallback
		// (see `ResolveMouseCanvasPos`) because the PlayerController
		// passed to our DebugDrawService callback is always nullptr —
		// engine broadcasts delegates with `PC=nullptr` — so we can't
		// rely on `PC->GetMousePosition`. The Slate path works in both
		// possessed PIE (when mouse is released via Shift+F1 or UI input
		// mode) and F8-ejected PIE (mouse naturally released).
		int32 HoverIdx = -1;
		float MouseX = 0.f, MouseY = 0.f;
		if (ResolveMouseCanvasPos(Canvas, MouseX, MouseY) && History.Num() >= 2)
		{
			const float RowsBottomY = RowsTopY + KPoseHistoryRowH * KPoseHistoryNumRows;
			const bool bMouseInPlot =
				MouseX >= PlotLeft && MouseX <= PlotRight &&
				MouseY >= RowsTopY  && MouseY <= RowsBottomY;
			if (bMouseInPlot)
			{
				const float NormX = (MouseX - PlotLeft) / FMath::Max(1.f, PlotRight - PlotLeft);
				HoverIdx = FMath::Clamp(
					FMath::RoundToInt(NormX * static_cast<float>(History.Num() - 1)),
					0, History.Num() - 1);
			}
		}

		// Six sparkline specs. Two formatter lambdas — position values
		// up to ~99999.9 use `%8.1f`, rotations span -180..+180 so
		// `%+7.1f` gives a signed 7-wide column. Both lambdas embed the
		// format literal inline so UE 5.6's consteval format check
		// accepts them (stored as TFunction in the spec; a runtime
		// `const TCHAR*` would not compile).
		const auto FormatPos = [](float V) -> FString { return FString::Printf(TEXT("%8.1f"), V); };
		const auto FormatRot = [](float V) -> FString { return FString::Printf(TEXT("%+7.1f"), V); };

		const FSparklineSpec Specs[KPoseHistoryNumRows] = {
			{ TEXT("Pos.X"), [](const FComposableCameraPoseHistoryEntry& E){ return static_cast<float>(E.Position.X);    }, FormatPos },
			{ TEXT("Pos.Y"), [](const FComposableCameraPoseHistoryEntry& E){ return static_cast<float>(E.Position.Y);    }, FormatPos },
			{ TEXT("Pos.Z"), [](const FComposableCameraPoseHistoryEntry& E){ return static_cast<float>(E.Position.Z);    }, FormatPos },
			{ TEXT("Rot.P"), [](const FComposableCameraPoseHistoryEntry& E){ return static_cast<float>(E.Rotation.Pitch); }, FormatRot },
			{ TEXT("Rot.Y"), [](const FComposableCameraPoseHistoryEntry& E){ return static_cast<float>(E.Rotation.Yaw);   }, FormatRot },
			{ TEXT("Rot.R"), [](const FComposableCameraPoseHistoryEntry& E){ return static_cast<float>(E.Rotation.Roll);  }, FormatRot },
		};

		for (int32 Row = 0; Row < KPoseHistoryNumRows; ++Row)
		{
			const FVector2D RowPos(RowsLeft, RowsTopY + Row * KPoseHistoryRowH);
			DrawSparklineRow(Canvas, BodyFont, RowPos, RowW, PlotLeft, PlotRight,
				History, Specs[Row], HoverIdx);
		}

		// Tooltip (hover only). Placed to the LEFT of the cursor if there's
		// room, else to the right — keeps it from clipping outside the
		// panel at the right edge.
		if (HoverIdx >= 0)
		{
			const FComposableCameraPoseHistoryEntry& E = History[HoverIdx];
			const float Now = History.Last().GameTime;
			const float Ago = Now - E.GameTime;

			const TArray<FString> Lines = {
				FString::Printf(TEXT("  t  -%.3fs"), Ago),
				FString::Printf(TEXT("  Pos  %.1f, %.1f, %.1f"), E.Position.X, E.Position.Y, E.Position.Z),
				FString::Printf(TEXT("  Rot  %+.1f, %+.1f, %+.1f"), E.Rotation.Pitch, E.Rotation.Yaw, E.Rotation.Roll),
				FString::Printf(TEXT("  FOV  %.1f"), E.FOVDegrees),
				FString::Printf(TEXT("  Ctx  %s"), *E.ContextName.ToString()),
			};

			// Measure the widest line for BG sizing.
			float MaxLineW = 0.f;
			for (const FString& L : Lines)
			{
				MaxLineW = FMath::Max(MaxLineW, MeasureTextWidth(Canvas, BodyFont, L));
			}
			const float TooltipW  = MaxLineW + 2.f * KPadding;
			const float TooltipH  = KLineH * Lines.Num() + 2.f * KPadding;

			// Position tooltip at the cursor. Prefer to the right; flip
			// if it'd overflow the panel.
			const float CursorScreenX = PlotLeft +
				static_cast<float>(HoverIdx) / static_cast<float>(FMath::Max(1, History.Num() - 1)) *
				(PlotRight - PlotLeft);
			float TooltipX = CursorScreenX + 8.f;
			if (TooltipX + TooltipW > PanelX + PanelW - KPadding)
			{
				TooltipX = CursorScreenX - 8.f - TooltipW;
			}
			const float TooltipY = FMath::Clamp(MouseY - TooltipH * 0.5f,
				RowsTopY, RowsTopY + KPoseHistoryRowH * KPoseHistoryNumRows - TooltipH);

			DrawFilledRect(Canvas, FVector2D(TooltipX, TooltipY),
				FVector2D(TooltipW, TooltipH),
				FLinearColor(0.05f, 0.05f, 0.08f, 0.90f));
			DrawBorder    (Canvas, FVector2D(TooltipX, TooltipY),
				FVector2D(TooltipW, TooltipH), CBorder, 1.f);
			for (int32 i = 0; i < Lines.Num(); ++i)
			{
				DrawTextLineClipped(Canvas, BodyFont, Lines[i],
					TooltipX + KPadding, TooltipY + KPadding + i * KLineH,
					TooltipX + TooltipW - KPadding, CValue);
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────
	// Layout driver.
	// Computes each region's content lines once, then runs two passes:
	//   1. height pass: sum region heights, decide panel height.
	//   2. draw pass:   paint panel BG, then per-region title bar + body.
	// ─────────────────────────────────────────────────────────────────
	static void DrawPanel(UCanvas* Canvas, APlayerController* PC)
	{
		if (CVarPanelEnabled.GetValueOnGameThread() == 0) { return; }
		if (!Canvas || Canvas->SizeX <= 0 || Canvas->SizeY <= 0) { return; }

		// One-time trace — if this line never fires in the log, the delegate
		// is not being called at all (show-flag / registration / module-load
		// timing problem). If it fires but no panel appears, the downstream
		// PCM / rendering path is at fault.
		static bool bLoggedFirstFire = false;
		if (!bLoggedFirstFire)
		{
			bLoggedFirstFire = true;
			UE_LOG(LogComposableCameraSystem, Log,
				TEXT("CCS.Debug.Panel: draw delegate fired for the first time (Canvas=%dx%d, PC=%s)."),
				Canvas->SizeX, Canvas->SizeY,
				PC ? *PC->GetName() : TEXT("nullptr"));
		}

		AComposableCameraPlayerCameraManager* PCM = ResolvePCM(PC);
		if (!PCM)
		{
			// In editor-idle (no PIE/Game world) the `"Editor"` channel
			// still fires our delegate each frame — that's expected, not
			// an error. Staying silent here keeps the editor viewport
			// clean while still giving us a visible red banner when PIE
			// IS running but the PCM genuinely isn't wired up (a real
			// setup bug worth surfacing).
			if (!HasPIEOrGameWorld()) { return; }

			// PIE / Game world exists but no CCS PCM found — surface the
			// problem visibly. The banner disappears the moment a CCS PCM
			// comes online.
			UFont* BodyFont = GEngine ? GEngine->GetSmallFont() : nullptr;
			if (!BodyFont) { return; }
			const FString Text = TEXT("CCS.Debug.Panel: no AComposableCameraPlayerCameraManager found in PIE / Game world.");
			const FVector2D Pos(KMargin, KMargin);
			const FVector2D Size(720.f, 22.f);
			DrawFilledRect(Canvas, Pos, Size, FLinearColor(0.3f, 0.05f, 0.05f, 0.85f));
			DrawBorder    (Canvas, Pos, Size, FLinearColor(0.9f, 0.4f, 0.4f, 0.9f), 1.f);
			DrawTextLineClipped(Canvas, BodyFont, Text, Pos.X + KPadding, Pos.Y + 4.f,
				Pos.X + Size.X - KPadding, FLinearColor(1.f, 0.8f, 0.8f, 1.f));
			return;
		}

		FPanelCtx Ctx;
		Ctx.Canvas     = Canvas;
		Ctx.PCM        = PCM;
		Ctx.HeaderFont = GEngine->GetMediumFont();
		Ctx.BodyFont   = GEngine->GetSmallFont();
		if (!Ctx.HeaderFont || !Ctx.BodyFont) { return; }

		// Build the structured Context-Stack snapshot once per frame. This is
		// the data source for the Stack & Tree region — structured fields
		// enable progress bars, indent stems, and dominant-leaf highlighting
		// that cannot be expressed through the line-based Lines path.
		FComposableCameraContextStackSnapshot StackSnapshot;
		if (const UComposableCameraContextStack* Stack = PCM->GetContextStack())
		{
			Stack->BuildDebugSnapshot(StackSnapshot);
		}

		// Build the legend rows once so we know whether to include the
		// region at all — empty legend → no region, no wasted title bar.
		// Gated by CCS.Debug.Panel.Legend (default on): zeroing it hides
		// the legend unconditionally even when gizmos are active.
		TArray<TPair<FString, FLinearColor>> LegendTransRows;
		TArray<TPair<FString, FLinearColor>> LegendNodeRows;
		const bool bWantLegend = CVarPanelLegend.GetValueOnGameThread() != 0;
		if (bWantLegend)
		{
			BuildLegendRows(LegendTransRows, LegendNodeRows);
		}
		const float LegendH = bWantLegend
			? ComputeLegendBodyHeight(LegendTransRows, LegendNodeRows)
			: 0.f;
		const bool bHasLegend = LegendH > 0.f;

		// Build the warnings region's lines up front so we know whether
		// there are any entries worth showing. Gated by
		// CCS.Debug.Panel.Warnings (default on): zeroing hides unconditionally.
		FRegionLines WarningsRegion;
		bool bHasWarnings = false;
		if (CVarPanelWarnings.GetValueOnGameThread() != 0)
		{
			bHasWarnings = BuildWarningsLines(WarningsRegion);
		}

		// Build all region descriptors up front.
		// Region order (top → bottom): pose / stack / running camera /
		// actions / modifiers / patches / warnings / legend. Patches sits next
		// to its sibling "what's affecting the camera" data (Actions, Modifiers).
		// Warnings sits above Legend so "something is wrong" gets more screen
		// weight than "here's the color key".
		const bool bWantPatches = CVarPanelPatches.GetValueOnGameThread() != 0;
		const int32 NumRegions = 5
			+ (bWantPatches ? 1 : 0)
			+ (bHasWarnings ? 1 : 0)
			+ (bHasLegend ? 1 : 0);
		TArray<FRegionLines, TInlineAllocator<9>> Regions;
		Regions.SetNum(NumRegions);
		BuildPoseGroups         (Ctx, Regions[0]);

		// Region[1] — Context Stack & Evaluation Tree (structured, not text).
		Regions[1].Title            = TEXT("Context Stack & Evaluation Tree");
		Regions[1].bIsStackAndTree  = true;
		Regions[1].StackBodyHeight  = ComputeStackBodyHeight(StackSnapshot);

		BuildRunningCameraLines (Ctx, Regions[2]);
		BuildActionsLines       (Ctx, Regions[3]);
		BuildModifiersLines     (Ctx, Regions[4]);

		int32 NextRegionIdx = 5;
		if (bWantPatches)
		{
			BuildPatchesLines(Ctx, Regions[NextRegionIdx++]);
		}
		if (bHasWarnings)
		{
			Regions[NextRegionIdx++] = MoveTemp(WarningsRegion);
		}
		if (bHasLegend)
		{
			Regions[NextRegionIdx].Title            = TEXT("Legend");
			Regions[NextRegionIdx].bIsLegend        = true;
			Regions[NextRegionIdx].LegendBodyHeight = LegendH;
			++NextRegionIdx;
		}

		// Layout: left-aligned panel, width = clamped CVar fraction of screen width.
		const float ScreenW = static_cast<float>(Canvas->SizeX);
		const float ScreenH = static_cast<float>(Canvas->SizeY);
		const float PanelW  = FMath::Clamp(CVarPanelWidth.GetValueOnGameThread(), 0.15f, 0.60f) * ScreenW;
		const float PanelX  = KMargin;
		float       PanelY  = KMargin;

		// Height pass. Structured regions use their pre-computed body height.
		auto RegionBodyH = [](const FRegionLines& R) -> float
		{
			float RawH;
			if (R.bIsStackAndTree)      { RawH = R.StackBodyHeight; }
			else if (R.bIsLegend)       { RawH = R.LegendBodyHeight; }
			else if (R.bIsPose)         { RawH = R.PoseBodyHeight; }
			else if (R.bIsPatches)      { RawH = R.PatchesBodyHeight; }
			else                        { RawH = R.Lines.Num() * KLineH; }
			return RawH + KPadding * 2.f;
		};

		float TotalH = 0.f;
		for (const FRegionLines& R : Regions)
		{
			TotalH += KTitleBarH + RegionBodyH(R) + KInterRegionGap;
		}
		if (TotalH > 0.f) { TotalH -= KInterRegionGap; } // trim trailing gap
		// Clamp to screen.
		const float MaxH = ScreenH - 2.f * KMargin;
		const float PanelH = FMath::Min(TotalH + KPadding * 2.f, MaxH);

		// Panel backdrop + outer border.
		DrawFilledRect(Canvas, FVector2D(PanelX, PanelY), FVector2D(PanelW, PanelH), CPanelBG);
		DrawBorder    (Canvas, FVector2D(PanelX, PanelY), FVector2D(PanelW, PanelH), CBorder, 1.5f);

		// Draw pass.
		float CursorY = PanelY + KPadding;
		const float RegionX = PanelX + KPadding;
		const float RegionW = PanelW - 2.f * KPadding;

		for (const FRegionLines& R : Regions)
		{
			const float BodyH = RegionBodyH(R);
			const float RegionH = KTitleBarH + BodyH;

			// Clip if we're out of vertical room.
			if (CursorY + KTitleBarH > PanelY + PanelH - KPadding) { break; }

			// Title bar only — the content area below reads straight through
			// the outer panel BG (single-layer translucency, game stays visible).
			DrawFilledRect(Canvas, FVector2D(RegionX, CursorY), FVector2D(RegionW, KTitleBarH), CTitleBG);
			DrawTextLineClipped(Canvas, Ctx.HeaderFont, R.Title,
				RegionX + KPadding, CursorY + 2.f,
				RegionX + RegionW - KPadding, CTitle);

			// Region border (outline only — 1px, negligible coverage).
			DrawBorder(Canvas, FVector2D(RegionX, CursorY), FVector2D(RegionW, RegionH), CBorder, 1.f);

			// Body: structured tree, legend, or flat text lines.
			const FVector2D BodyPos(RegionX + KPadding, CursorY + KTitleBarH + KPadding);
			const FVector2D BodySize(RegionW - 2.f * KPadding, BodyH - 2.f * KPadding);
			if (R.bIsStackAndTree)
			{
				DrawStackAndTreeStructured(Ctx, StackSnapshot, BodyPos, BodySize);
			}
			else if (R.bIsLegend)
			{
				DrawLegendStructured(Ctx, LegendTransRows, LegendNodeRows, BodyPos, BodySize);
			}
			else if (R.bIsPose)
			{
				DrawPoseStructured(Ctx, R, BodyPos, BodySize);
			}
			else if (R.bIsPatches)
			{
				DrawPatchesStructured(Ctx, R.PatchSnapshots, BodyPos, BodySize);
			}
			else
			{
				const float MaxBodyY = BodyPos.Y + BodySize.Y;
				const float BodyRightX = BodyPos.X + BodySize.X;

				// Two-pass render with per-group Value-column alignment.
				//
				// Rationale: the Running Camera region mixes a short basic-info
				// block ("Class" / "Tag" / "Life") with a Data Block section
				// whose labels are much longer ("Exposed params", "Internal
				// vars"), plus Parameters / Variables with arbitrarily long
				// user-defined names. A single region-wide MaxLabelPx would
				// push "Class"' value far to the right to make room for the
				// longest Parameter name — visually ugly. So we break rows
				// into GROUPS separated by full-line rows (empty Label —
				// section headers / placeholders / list items), compute
				// MaxLabelPx per group, and align only within each group.
				// Proportional font ⇒ pixel-measure is the only way to get
				// clean alignment; padding labels with spaces at build time
				// aligns on monospace only.
				//
				// `RowValueX[i]` stores the Value-column X for row i. Full-
				// line rows get `BodyPos.X` (they render unindented across
				// the whole body).
				constexpr float KLabelValueGap = 10.f;
				TArray<float, TInlineAllocator<64>> RowValueX;
				RowValueX.SetNum(R.Lines.Num());

				int32 GroupStart = 0;
				float GroupMaxLabelPx = 0.f;
				auto FlushGroup = [&](int32 EndExclusive)
				{
					const float VX = (GroupMaxLabelPx > 0.f)
						? BodyPos.X + GroupMaxLabelPx + KLabelValueGap
						: BodyPos.X;
					for (int32 j = GroupStart; j < EndExclusive; ++j)
					{
						RowValueX[j] = VX;
					}
					GroupStart = EndExclusive + 1;
					GroupMaxLabelPx = 0.f;
				};
				for (int32 i = 0; i < R.Lines.Num(); ++i)
				{
					const FPanelLine& L = R.Lines[i];
					if (L.Label.IsEmpty())
					{
						FlushGroup(i);
						RowValueX[i] = BodyPos.X;
					}
					else
					{
						GroupMaxLabelPx = FMath::Max(GroupMaxLabelPx,
							MeasureTextWidth(Canvas, Ctx.BodyFont, L.Label));
					}
				}
				if (GroupStart < R.Lines.Num())
				{
					FlushGroup(R.Lines.Num());
				}

				float LineY = BodyPos.Y;
				for (int32 i = 0; i < R.Lines.Num(); ++i)
				{
					if (LineY + KLineH > MaxBodyY) { break; }
					const FPanelLine& L = R.Lines[i];
					if (L.Label.IsEmpty())
					{
						DrawTextLineClipped(Canvas, Ctx.BodyFont, L.Value,
							BodyPos.X, LineY, BodyRightX, L.Color);
					}
					else
					{
						const float VX = RowValueX[i];
						DrawTextLineClipped(Canvas, Ctx.BodyFont, L.Label,
							BodyPos.X, LineY, VX, L.Color);
						DrawTextLineClipped(Canvas, Ctx.BodyFont, L.Value,
							VX, LineY, BodyRightX, L.Color);
					}
					LineY += KLineH;
				}
			}

			CursorY += RegionH + KInterRegionGap;
		}
	}
} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────
// Dual-channel registration + per-(frame, FCanvas) dedup.
//
// Problem: `UDebugDrawService::Register(TEXT("Game"), ...)` only fires
// when the active viewport's `FEngineShowFlags.Game` is on. That's the
// runtime case (PIE Selected Viewport, packaged game). When the user
// presses F8 in PIE, the viewport swaps to an editor-style viewport
// whose ShowFlags have `Game = false` and `Editor = true`, so the
// "Game"-channel delegate stops firing and our debug panels disappear.
//
// Solution: register each panel's draw delegate twice — once on
// `"Game"` and once on `"Editor"`.
//
// Why the dedup key must be `FCanvas*`, not `UCanvas*`:
//   `UDebugDrawService` reuses a SINGLE `UCanvas` transient singleton
//   (`FindObject<UCanvas>(GetTransientPackage(), TEXT("DebugCanvasObject"))`)
//   for every draw call, re-`Init`-ing it per call with the current
//   viewport's `FCanvas*` and `FSceneView*`. So the `UCanvas*` pointer
//   is IDENTICAL across every viewport's draw, which would make a
//   `UCanvas*`-keyed dedup treat all viewports as "same canvas" and
//   skip every call after the first one in a frame.
//
//   The underlying `FCanvas*` (stored in `UCanvas::Canvas`) does differ
//   per viewport draw — each viewport renders with its own FCanvas on
//   the stack for that render pass. That's the right dedup axis.
//
// Flicker symptom this fixed: in F8-ejected PIE, the hidden game
// viewport and the visible editor viewport each issue their own
// `UDebugDrawService::Draw` call per frame. With a `UCanvas*`-keyed
// dedup, the first call in a frame (whichever viewport happened to
// draw first) "claimed" the frame and every subsequent call skipped —
// so if the Game-channel call fired first (drawing on the invisible
// canvas) the user saw nothing that frame. When the mouse moved, Slate
// invalidated widgets at irregular intervals and re-ordered viewport
// draws, flipping which viewport got the frame's one draw — visible /
// invisible alternation → per-frame flicker tied to mouse motion.
// Same-frame + same-FCanvas hits (both ShowFlags on for one viewport
// in a transient state) are the legitimate dedup case.
//
// Dedup is per-panel (two independent state pairs) so the main panel
// and pose-history panel can't mask each other's re-entry.
//
// Namespace-anonymous globals are fine here because the module
// Initialize / Shutdown own their full lifecycle — no GC tracking,
// no serialization.
// ─────────────────────────────────────────────────────────────────────
namespace
{
	FDelegateHandle GMainPanelGameHandle;
	FDelegateHandle GMainPanelEditorHandle;
	FDelegateHandle GPoseHistoryGameHandle;
	FDelegateHandle GPoseHistoryEditorHandle;

	uint64            GLastMainPanelFrame      = 0;
	const FCanvas*    GLastMainPanelCanvas     = nullptr;
	uint64            GLastPoseHistoryFrame    = 0;
	const FCanvas*    GLastPoseHistoryCanvas   = nullptr;

	/** Per-viewport canvas identity key. `UCanvas` is a reused singleton
	 *  (see big comment above) but `UCanvas::Canvas` is the underlying
	 *  `FCanvas*` for THIS draw call's viewport, which is what we actually
	 *  want to dedup on. */
	static const FCanvas* GetDedupCanvasKey(const UCanvas* Canvas)
	{
		return Canvas ? Canvas->Canvas : nullptr;
	}

	/** Dedup wrapper for the main context-stack panel. See the big comment
	 *  above for why the dedup key is (frame, FCanvas*) and not just frame. */
	void DrawPanel_Dedup(UCanvas* Canvas, APlayerController* PC)
	{
		const uint64 Frame = GFrameCounter;
		const FCanvas* CanvasKey = GetDedupCanvasKey(Canvas);
		if (Frame == GLastMainPanelFrame && CanvasKey == GLastMainPanelCanvas) { return; }
		GLastMainPanelFrame  = Frame;
		GLastMainPanelCanvas = CanvasKey;
		DrawPanel(Canvas, PC);
	}

	/** Dedup wrapper for the right-side pose-history panel. */
	void DrawPoseHistoryPanel_Dedup(UCanvas* Canvas, APlayerController* PC)
	{
		const uint64 Frame = GFrameCounter;
		const FCanvas* CanvasKey = GetDedupCanvasKey(Canvas);
		if (Frame == GLastPoseHistoryFrame && CanvasKey == GLastPoseHistoryCanvas) { return; }
		GLastPoseHistoryFrame  = Frame;
		GLastPoseHistoryCanvas = CanvasKey;
		DrawPoseHistoryPanel(Canvas, PC);
	}
}

// ─────────────────────────────────────────────────────────────────────
// Public lifecycle.
// Called from the runtime module's StartupModule / ShutdownModule.
// ─────────────────────────────────────────────────────────────────────
void FComposableCameraDebugPanel::Initialize()
{
	// Main context-stack + modifier + logs panel — dual-channel.
	if (!GMainPanelGameHandle.IsValid())
	{
		GMainPanelGameHandle = UDebugDrawService::Register(
			TEXT("Game"),
			FDebugDrawDelegate::CreateStatic(&DrawPanel_Dedup));
	}
	if (!GMainPanelEditorHandle.IsValid())
	{
		GMainPanelEditorHandle = UDebugDrawService::Register(
			TEXT("Editor"),
			FDebugDrawDelegate::CreateStatic(&DrawPanel_Dedup));
	}

	// Pose History panel — also dual-channel. Registered separately so
	// `CCS.Debug.Panel` and `CCS.Debug.Panel.PoseHistory` CVars toggle
	// independently: either panel can be on without the other.
	if (!GPoseHistoryGameHandle.IsValid())
	{
		GPoseHistoryGameHandle = UDebugDrawService::Register(
			TEXT("Game"),
			FDebugDrawDelegate::CreateStatic(&DrawPoseHistoryPanel_Dedup));
	}
	if (!GPoseHistoryEditorHandle.IsValid())
	{
		GPoseHistoryEditorHandle = UDebugDrawService::Register(
			TEXT("Editor"),
			FDebugDrawDelegate::CreateStatic(&DrawPoseHistoryPanel_Dedup));
	}
}

void FComposableCameraDebugPanel::Shutdown()
{
	if (GMainPanelGameHandle.IsValid())
	{
		UDebugDrawService::Unregister(GMainPanelGameHandle);
		GMainPanelGameHandle.Reset();
	}
	if (GMainPanelEditorHandle.IsValid())
	{
		UDebugDrawService::Unregister(GMainPanelEditorHandle);
		GMainPanelEditorHandle.Reset();
	}
	if (GPoseHistoryGameHandle.IsValid())
	{
		UDebugDrawService::Unregister(GPoseHistoryGameHandle);
		GPoseHistoryGameHandle.Reset();
	}
	if (GPoseHistoryEditorHandle.IsValid())
	{
		UDebugDrawService::Unregister(GPoseHistoryEditorHandle);
		GPoseHistoryEditorHandle.Reset();
	}
}
