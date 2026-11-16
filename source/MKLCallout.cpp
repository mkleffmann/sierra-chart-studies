// MKLCallout.cpp

#include "sierrachart.h"

#include <vector>
#include <string>
#include <cmath>

SCDLLName("MKL Callout");

using SCPen = n_ACSIL::s_GraphicsPen;
using SCPenStyle = n_ACSIL::s_GraphicsPen::e_PenStyle;
using SCBrush = n_ACSIL::s_GraphicsBrush;
using SCBrushType = n_ACSIL::s_GraphicsBrush::e_BrushType;
using SCColor = n_ACSIL::s_GraphicsColor;
using SCRect = n_ACSIL::s_GraphicsRectangle;
using SCPoint = n_ACSIL::s_GraphicsPoint;
using SCFont = n_ACSIL::s_GraphicsFont;
using SCSize = n_ACSIL::s_GraphicsSize;


/*==================================================================================*/
// Returns true if the Arrow drawing contains text.
//
static bool ArrowHasText(const s_UseTool& Arrow)
{
	const char* Text = Arrow.Text.GetChars();

	return Text != nullptr
		&& Text[0] != '\0';
}


/*==================================================================================*/
// Splits Arrow text into individual lines.
//
// Supports Windows CR/LF and LF line endings.
// Empty lines are preserved.
//
static std::vector<std::string> SplitTextIntoLines(
	const SCString& Text)
{
	std::vector<std::string> Lines;

	const char* Characters =
		Text.GetChars();

	if (Characters == nullptr)
		return Lines;

	std::string CurrentLine;

	for (const char* Character = Characters;
		*Character != '\0';
		++Character)
	{
		// Ignore CR.
		if (*Character == '\r')
			continue;

		// LF starts a new line.
		if (*Character == '\n')
		{
			Lines.push_back(CurrentLine);
			CurrentLine.clear();
			continue;
		}

		CurrentLine += *Character;
	}

	Lines.push_back(CurrentLine);

	return Lines;
}


/*==================================================================================*/
// Shows or hides all native Sierra Arrow drawings containing text.
//
// Plain arrows without text are deliberately ignored.
//
static void SetNativeArrowVisibility(
	SCStudyInterfaceRef sc,
	const bool Hide)
{
	std::vector<int> LineNumbers;

	s_UseTool Arrow;
	Arrow.Clear();

	int DrawingIndex = 0;

	while (sc.GetUserDrawnChartDrawing(
		sc.ChartNumber,
		DRAWING_UNKNOWN,
		Arrow,
		DrawingIndex++) != 0)
	{
		if (Arrow.DrawingType != DRAWING_ARROW)
			continue;

		if (!ArrowHasText(Arrow))
			continue;

		if ((Arrow.HideDrawing != 0) == Hide)
			continue;

		LineNumbers.push_back(
			Arrow.LineNumber);
	}


	// Modify drawings only after enumeration.
	for (const int LineNumber : LineNumbers)
	{
		s_UseTool Tool;
		Tool.Clear();

		Tool.ChartNumber =
			sc.ChartNumber;

		Tool.LineNumber =
			LineNumber;

		Tool.AddAsUserDrawnDrawing = 1;
		Tool.AddMethod = UTAM_ADD_OR_ADJUST;

		Tool.HideDrawing =
			Hide ? 1 : 0;

		sc.UseTool(Tool);
	}
}


/*==================================================================================*/
// Calculates where the original Sierra Arrow line exits the Callout box.
//
// StartX/StartY is the native Arrow text anchor and is expected to be inside the box.
// TargetX/TargetY is the Arrow tip outside the box.
//
// The returned point lies on the box boundary and on the exact same line as Sierra's
// original Arrow. This means the portion of the Arrow that would pass underneath the
// transparent box is never rendered at all.
//
static bool GetRectangleExitPoint(
	const int Left,
	const int Top,
	const int Right,
	const int Bottom,
	const int StartX,
	const int StartY,
	const int TargetX,
	const int TargetY,
	int& ExitX,
	int& ExitY)
{
	const double DeltaX =
		TargetX - StartX;

	const double DeltaY =
		TargetY - StartY;

	if (std::abs(DeltaX) < 0.001
		&& std::abs(DeltaY) < 0.001)
	{
		return false;
	}


	// We move from the point inside the rectangle toward the target:
	//
	//     P(t) = Start + t * (Target - Start)
	//
	// and choose the first positive t at which P reaches one of the four edges.
	double ExitScale = 1.0e30;


	if (DeltaX > 0.001)
	{
		const double Scale =
			(Right - StartX)
			/ DeltaX;

		if (Scale >= 0.0
			&& Scale < ExitScale)
		{
			ExitScale = Scale;
		}
	}
	else if (DeltaX < -0.001)
	{
		const double Scale =
			(Left - StartX)
			/ DeltaX;

		if (Scale >= 0.0
			&& Scale < ExitScale)
		{
			ExitScale = Scale;
		}
	}


	if (DeltaY > 0.001)
	{
		const double Scale =
			(Bottom - StartY)
			/ DeltaY;

		if (Scale >= 0.0
			&& Scale < ExitScale)
		{
			ExitScale = Scale;
		}
	}
	else if (DeltaY < -0.001)
	{
		const double Scale =
			(Top - StartY)
			/ DeltaY;

		if (Scale >= 0.0
			&& Scale < ExitScale)
		{
			ExitScale = Scale;
		}
	}


	if (ExitScale == 1.0e30)
		return false;


	// If the target itself is still inside the box, there is no visible external
	// Arrow segment to draw.
	if (ExitScale >= 1.0)
		return false;


	ExitX =
		static_cast<int>(
			std::round(
				StartX
				+ DeltaX * ExitScale));

	ExitY =
		static_cast<int>(
			std::round(
				StartY
				+ DeltaY * ExitScale));

	return true;
}

/*==================================================================================*/
// Draws a line with a filled triangular arrowhead.
//
static void DrawArrow(
	SCStudyInterfaceRef sc,
	const int StartX,
	const int StartY,
	const int EndX,
	const int EndY,
	const SCColor& Color)
{
	const double DeltaX =
		EndX - StartX;

	const double DeltaY =
		EndY - StartY;

	const double Length =
		std::sqrt(
			DeltaX * DeltaX
			+ DeltaY * DeltaY);

	if (Length < 2.0)
		return;


	const double UnitX =
		DeltaX / Length;

	const double UnitY =
		DeltaY / Length;


	// -------------------------------------------------------------------------
	// Arrow dimensions
	// -------------------------------------------------------------------------

	const double ArrowHeadLength = 10.0;
	const double ArrowHeadHalfWidth = 4.5;


	// Base center of the arrowhead.
	const double BaseCenterX =
		EndX
		- UnitX * ArrowHeadLength;

	const double BaseCenterY =
		EndY
		- UnitY * ArrowHeadLength;


	// Perpendicular vector.
	const double PerpendicularX =
		-UnitY;

	const double PerpendicularY =
		UnitX;


	// -------------------------------------------------------------------------
	// Arrow shaft
	// -------------------------------------------------------------------------

	SCPen ArrowPen;

	ArrowPen.m_PenColor =
		Color;

	ArrowPen.m_PenStyle =
		SCPenStyle::PEN_STYLE_SOLID;

	ArrowPen.m_Width = 2;


	sc.Graphics.SetPen(
		ArrowPen);


	// Draw shaft only up to the beginning of the arrowhead.
	sc.Graphics.MoveTo(
		StartX,
		StartY);

	sc.Graphics.LineTo(
		static_cast<int>(
			std::round(BaseCenterX)),
		static_cast<int>(
			std::round(BaseCenterY)));


	// -------------------------------------------------------------------------
	// Filled arrowhead
	// -------------------------------------------------------------------------

	SCPoint ArrowHead[3];


	// Tip
	ArrowHead[0].X =
		EndX;

	ArrowHead[0].Y =
		EndY;


	// Left base point
	ArrowHead[1].X =
		static_cast<int>(
			std::round(
				BaseCenterX
				+ PerpendicularX
				* ArrowHeadHalfWidth));

	ArrowHead[1].Y =
		static_cast<int>(
			std::round(
				BaseCenterY
				+ PerpendicularY
				* ArrowHeadHalfWidth));


	// Right base point
	ArrowHead[2].X =
		static_cast<int>(
			std::round(
				BaseCenterX
				- PerpendicularX
				* ArrowHeadHalfWidth));

	ArrowHead[2].Y =
		static_cast<int>(
			std::round(
				BaseCenterY
				- PerpendicularY
				* ArrowHeadHalfWidth));


	SCBrush ArrowBrush;

	ArrowBrush.m_BrushType =
		SCBrushType::BRUSH_TYPE_SOLID;

	ArrowBrush.m_BrushColor =
		Color;


	SCPen NullPen;

	NullPen.m_PenStyle =
		SCPenStyle::PEN_STYLE_NULL;


	sc.Graphics.SetBrush(
		ArrowBrush);

	sc.Graphics.SetPen(
		NullPen);

	sc.Graphics.Internal_DrawPolygon(
		ArrowHead,
		3);
}


/*==================================================================================*/
// Draws one MKL Callout.
//
static void DrawCallout(
	SCStudyInterfaceRef sc,
	const s_UseTool& Arrow)
{
	if (!ArrowHasText(Arrow))
		return;

	if (Arrow.BeginIndex < 0
		|| Arrow.EndIndex < 0)
		return;


	// -------------------------------------------------------------------------
	// Layout
	// -------------------------------------------------------------------------

	const int PaddingLeft = 14;
	const int PaddingRight = 18;

	const int PaddingTop = 11;
	const int PaddingBottom = 15;


	// -------------------------------------------------------------------------
	// Font
	//
	// Defaults match the user's Sierra Global Chart Drawing Font:
	// Arial, 11 pt, Bold. Font Name / Size / Bold can be configured in the
	// Study Settings if Sierra's global drawing font changes.
	// -------------------------------------------------------------------------

	SCFont Font;

	Font.m_FaceName =
		sc.Input[1].GetString();

	Font.m_Height =
		sc.Input[2].GetInt();

	Font.m_Weight =
		sc.Input[3].GetYesNo() != 0
		? FW_BOLD
		: FW_NORMAL;


	// Set the exact font before measuring or drawing.
	sc.Graphics.SetTextFont(
		Font);

	sc.Graphics.SetTextAlign(
		TA_LEFT
		| TA_TOP
		| TA_NOUPDATECP);


	// -------------------------------------------------------------------------
	// Split text into individual lines.
	// -------------------------------------------------------------------------

	const std::vector<std::string> Lines =
		SplitTextIntoLines(
			Arrow.Text);

	if (Lines.empty())
		return;


	// -------------------------------------------------------------------------
	// Determine line height.
	// -------------------------------------------------------------------------

	SCString HeightTestText;
	HeightTestText = "Ag";

	SCSize HeightTestSize;

	sc.Graphics.GetTextSize(
		HeightTestText,
		HeightTestSize);


	const int LineHeight =
		HeightTestSize.Height;


	// With the MKL font configured to match Sierra's native Chart Drawing
	// Font, the native Arrow text uses the full measured line height as its
	// vertical line advance. This also keeps intentionally blank lines aligned
	// with the native Sierra text.
	const int LineAdvance =
		LineHeight;


	// -------------------------------------------------------------------------
	// Measure all lines.
	//
	// Width of the box is determined by the longest line.
	// -------------------------------------------------------------------------

	int MaximumTextWidth = 0;


	for (const std::string& LineText : Lines)
	{
		if (LineText.empty())
			continue;


		SCString Line;
		Line = LineText.c_str();


		SCSize LineSize;

		sc.Graphics.GetTextSize(
			Line,
			LineSize);


		if (LineSize.Width
			> MaximumTextWidth)
		{
			MaximumTextWidth =
				LineSize.Width;
		}
	}


	// -------------------------------------------------------------------------
	// Box dimensions
	// -------------------------------------------------------------------------

	int TextHeight = 0;
	int LineOffset = 0;


	for (size_t LineIndex = 0;
		LineIndex < Lines.size();
		++LineIndex)
	{
		// A visible line needs enough room for its glyphs. A trailing blank
		// line still contributes one native-style line advance.
		const int LineBottom =
			LineOffset
			+ (Lines[LineIndex].empty()
				? LineAdvance
				: LineHeight);

		TextHeight = max(TextHeight, LineBottom);

		if (LineIndex + 1 < Lines.size())
			LineOffset += LineAdvance;
	}


	const int BoxWidth =
		MaximumTextWidth
		+ PaddingLeft
		+ PaddingRight;


	const int BoxHeight =
		TextHeight
		+ PaddingTop
		+ PaddingBottom;


	// -------------------------------------------------------------------------
	// Original Arrow coordinates
	//
	// Sierra anchors the native Arrow text at the Begin point. Therefore the
	// MKL text starts at exactly the same pixel coordinate instead of centering
	// the entire box around that point.
	//
	// Begin = native text anchor / start of MKL text
	// End   = arrow target
	// -------------------------------------------------------------------------

	const int TextAnchorX =
		sc.BarIndexToXPixelCoordinate(
			Arrow.BeginIndex);


	const int TextAnchorY =
		sc.RegionValueToYPixelCoordinate(
			Arrow.BeginValue,
			sc.GraphRegion);


	const int TargetX =
		sc.BarIndexToXPixelCoordinate(
			Arrow.EndIndex);


	const int TargetY =
		sc.RegionValueToYPixelCoordinate(
			Arrow.EndValue,
			sc.GraphRegion);


	// -------------------------------------------------------------------------
	// Box position
	//
	// Padding extends around the native text anchor. Consequently:
	//
	//   BoxLeft + PaddingLeft = TextAnchorX
	//   BoxTop  + PaddingTop  = TextAnchorY
	//
	// This keeps the MKL text at the same position as Sierra's native text.
	// The graphics clipping region safely handles boxes that extend beyond the
	// visible chart area, so the box is deliberately not repositioned.
	// -------------------------------------------------------------------------

	const int BoxLeft =
		TextAnchorX
		- PaddingLeft;


	const int BoxTop =
		TextAnchorY
		- PaddingTop;


	const int BoxRight =
		BoxLeft
		+ BoxWidth;


	const int BoxBottom =
		BoxTop
		+ BoxHeight;


	// -------------------------------------------------------------------------
	// Colors
	//
	// Arrow uses the original Sierra Arrow color and remains fully opaque.
	//
	// Box uses the same base color, but gets its own configurable transparency.
	// -------------------------------------------------------------------------

	SCColor CalloutColor;

	CalloutColor.SetColorValue(
		Arrow.Color);


	SCColor BoxColor;

	BoxColor.SetColorValue(
		Arrow.Color);


	// Use the same transparency convention as MKL Risk Reward:
	//
	// 0   = opaque
	// 100 = fully transparent
	const int BoxTransparency =
		sc.Input[4].GetInt();


	const auto BoxAlpha =
		static_cast<uint8_t>(
			BoxTransparency
			* 255
			* 0.01);


	BoxColor.Color.RGB.Alpha =
		BoxAlpha;


	SCColor TextColor;

	TextColor.SetColorValue(
		sc.Input[0].GetColor());


	// -------------------------------------------------------------------------
	// Draw arrow first so the box cleanly covers its starting point.
	// -------------------------------------------------------------------------

	// Draw only the part of the Arrow that lies OUTSIDE the Callout box.
	//
	// The native Sierra Arrow conceptually starts at TextAnchorX/TextAnchorY.
	// Because this point lies inside our transparent box, drawing the whole shaft
	// would make it visible through the box. Instead, calculate the exact point
	// where that same line exits the rectangle and start rendering there.
	int ArrowStartX = 0;
	int ArrowStartY = 0;


	if (GetRectangleExitPoint(
		BoxLeft,
		BoxTop,
		BoxRight,
		BoxBottom,
		TextAnchorX,
		TextAnchorY,
		TargetX,
		TargetY,
		ArrowStartX,
		ArrowStartY))
	{
		DrawArrow(
			sc,
			ArrowStartX,
			ArrowStartY,
			TargetX,
			TargetY,
			CalloutColor);
	}


	// -------------------------------------------------------------------------
	// Draw transparent rectangular text box.
	// -------------------------------------------------------------------------

	SCBrush BoxBrush;

	BoxBrush.m_BrushType =
		SCBrushType::BRUSH_TYPE_SOLID;

	BoxBrush.m_BrushColor =
		BoxColor;


	SCRect BoxRectangle;

	BoxRectangle.Left =
		BoxLeft;

	BoxRectangle.Top =
		BoxTop;

	BoxRectangle.Right =
		BoxRight;

	BoxRectangle.Bottom =
		BoxBottom;


	sc.Graphics.FillRectangle(
		BoxRectangle,
		BoxBrush);


	// -------------------------------------------------------------------------
	// Draw text.
	//
	// Text remains fully opaque.
	// DrawTextAt() avoids clipping long lines.
	// -------------------------------------------------------------------------

	sc.Graphics.SetTextFont(
		Font);


	sc.Graphics.SetTextColor(
		TextColor);


	sc.Graphics.SetTextAlign(
		TA_LEFT
		| TA_TOP
		| TA_NOUPDATECP);


	sc.Graphics.SetBackgroundMode(
		TRANSPARENT);


	const int TextX =
		BoxLeft
		+ PaddingLeft;


	int CurrentY =
		BoxTop
		+ PaddingTop;


	for (const std::string& LineText : Lines)
	{
		if (!LineText.empty())
		{
			SCString Line;
			Line = LineText.c_str();


			sc.Graphics.DrawTextAt(
				Line,
				TextX,
				CurrentY);
		}


		CurrentY += LineAdvance;
	}
}


/*==================================================================================*/
// Main graphics callback.
//
static void DrawCallouts(
	HWND WindowHandle,
	HDC DeviceContext,
	SCStudyInterfaceRef sc)
{
	// MKL Callouts are always rendered. Native Arrow visibility is controlled
	// independently by the Study Input / Control Bar button.
	if (sc.GetUserDrawnDrawingsCount(
		sc.ChartNumber,
		0) == 0)
		return;


	// -------------------------------------------------------------------------
	// Clip custom graphics to the chart region.
	// -------------------------------------------------------------------------

	SCRect ClippingRectangle;


	ClippingRectangle.Left =
		sc.StudyRegionLeftCoordinate;


	ClippingRectangle.Right =
		sc.StudyRegionRightCoordinate;


	ClippingRectangle.Top =
		sc.StudyRegionTopCoordinate;


	ClippingRectangle.Bottom =
		sc.StudyRegionBottomCoordinate;


	sc.Graphics.SetClippingRegionFromRectangle(
		ClippingRectangle);


	// -------------------------------------------------------------------------
	// Process all text-bearing Arrow drawings.
	// -------------------------------------------------------------------------

	s_UseTool Arrow;
	Arrow.Clear();


	int DrawingIndex = 0;


	while (sc.GetUserDrawnChartDrawing(
		sc.ChartNumber,
		DRAWING_UNKNOWN,
		Arrow,
		DrawingIndex++) != 0)
	{
		if (Arrow.DrawingType
			!= DRAWING_ARROW)
			continue;


		if (!ArrowHasText(Arrow))
			continue;


		DrawCallout(
			sc,
			Arrow);
	}


	// -------------------------------------------------------------------------
	// Restore graphics state.
	// -------------------------------------------------------------------------

	sc.Graphics.ResetTextFont();
	sc.Graphics.ResetTextColor();
	sc.Graphics.ResetTextAlign();
	sc.Graphics.ResetBackgroundMode();

	sc.Graphics.ResetPen();
	sc.Graphics.ResetBrush();

	sc.Graphics.ResetClippingRegion();
}


/*==================================================================================*/
SCSFExport scsf_MKLCallout(
	SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName =
			"MKL Callout";


		sc.GraphRegion = 0;
		sc.AutoLoop = 0;


		sc.ValueFormat =
			VALUEFORMAT_INHERITED;


		sc.ScaleRangeType =
			SCALE_SAMEASREGION;


		sc.GlobalDisplayStudySubgraphsNameAndValue = 0;
		sc.DisplayStudyInputValues = 0;
		sc.HideDLLAndFunctionNames = 1;


		sc.UpdateAlways = 1;


		// ---------------------------------------------------------------------
		// Inputs
		// ---------------------------------------------------------------------

		sc.Input[0].Name =
			"Text Color";


		sc.Input[0].SetColor(
			COLOR_WHITE);


		sc.Input[1].Name =
			"Font Name";


		sc.Input[1].SetString(
			"Arial");


		sc.Input[2].Name =
			"Font Size";


		sc.Input[2].SetInt(
			11);


		sc.Input[2].SetIntLimits(
			6,
			30);


		sc.Input[3].Name =
			"Font Bold";


		sc.Input[3].SetYesNo(
			1);


		sc.Input[4].Name =
			"Box Transparency";


		sc.Input[4].SetInt(
			40);


		sc.Input[4].SetIntLimits(
			0,
			100);


		// This controls ONLY the visibility of Sierra Chart's original
		// text-bearing Arrow drawings. MKL Callouts always remain visible.
		sc.Input[5].Name =
			"Show Native Arrow Drawing";


		sc.Input[5].SetYesNo(
			0);


		// Select one of Sierra Chart's ACS Control Bar buttons (1-150).
		// 0 disables Control Bar integration.
		sc.Input[6].Name =
			"Control Bar Button Number (0 = Disabled)";


		sc.Input[6].SetInt(
			0);


		sc.Input[6].SetIntLimits(
			0,
			150);

		return;
	}


	// Remember which button number was previously configured so that changing
	// the Study Input cannot leave the old button in an enabled state.
	int& LastControlBarButtonNumber =
		sc.GetPersistentIntFast(0);


	// -------------------------------------------------------------------------
	// Study removal:
	//
	// Restore all native text-bearing Arrow drawings so the user's drawings are
	// never left hidden after removing the study. Also release the Control Bar
	// button's On state.
	// -------------------------------------------------------------------------

	if (sc.LastCallToFunction)
	{
		SetNativeArrowVisibility(
			sc,
			false);


		if (LastControlBarButtonNumber != 0)
		{
			sc.SetCustomStudyControlBarButtonEnable(
				LastControlBarButtonNumber,
				0);
		}


		sc.p_GDIFunction =
			nullptr;


		return;
	}


	// -------------------------------------------------------------------------
	// Install graphics renderer.
	//
	// Important: The renderer is NOT tied to native-arrow visibility. Therefore
	// the MKL text box + arrow remain visible in both states.
	// -------------------------------------------------------------------------

	if (sc.UpdateStartIndex == 0)
	{
		sc.p_GDIFunction =
			DrawCallouts;
	}


	// -------------------------------------------------------------------------
	// Control Bar button configuration
	// -------------------------------------------------------------------------

	const int ControlBarButtonNumber =
		sc.Input[6].GetInt();


	// If the configured button number changed, ensure the old button is no
	// longer left in the On state.
	if (LastControlBarButtonNumber != ControlBarButtonNumber)
	{
		if (LastControlBarButtonNumber != 0)
		{
			sc.SetCustomStudyControlBarButtonEnable(
				LastControlBarButtonNumber,
				0);
		}


		LastControlBarButtonNumber =
			ControlBarButtonNumber;
	}


	if (ControlBarButtonNumber != 0)
	{
		// Caption and tooltip are configured in Sierra Chart's
		// Control Bar Button Properties, not by this study.

		// A manual button press directly updates the matching Study Input.
		// Button ON  = native text-bearing Arrow drawings visible.
		// Button OFF = native text-bearing Arrow drawings hidden.
		if (sc.MenuEventID == ControlBarButtonNumber)
		{
			if (sc.PointerEventType == SC_ACS_BUTTON_ON)
				sc.Input[5].SetYesNo(1);
			else if (sc.PointerEventType == SC_ACS_BUTTON_OFF)
				sc.Input[5].SetYesNo(0);
		}
	}


	// -------------------------------------------------------------------------
	// Native Arrow visibility
	// -------------------------------------------------------------------------

	const bool ShowNativeArrowDrawing =
		sc.Input[5].GetYesNo() != 0;


	SetNativeArrowVisibility(
		sc,
		!ShowNativeArrowDrawing);


	// -------------------------------------------------------------------------
	// Keep the Control Bar button visually synchronized with the Study Input.
	// Changing the Input manually in Study Settings therefore also updates the
	// button state after Apply/OK.
	// -------------------------------------------------------------------------

	if (ControlBarButtonNumber != 0)
	{
		const int CurrentButtonState =
			sc.GetCustomStudyControlBarButtonEnableState(
				ControlBarButtonNumber);


		const int DesiredButtonState =
			ShowNativeArrowDrawing ? 1 : 0;


		if (CurrentButtonState != DesiredButtonState)
		{
			sc.SetCustomStudyControlBarButtonEnable(
				ControlBarButtonNumber,
				DesiredButtonState);
		}
	}
}
