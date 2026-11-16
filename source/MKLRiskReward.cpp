// MKLRiskReward.cpp

#include "sierrachart.h"
#include <vector>

SCDLLName("MKL Risk Reward");

using SCPen = n_ACSIL::s_GraphicsPen;
using SCPenStyle = n_ACSIL::s_GraphicsPen::e_PenStyle;
using SCBrush = n_ACSIL::s_GraphicsBrush;
using SCBrushType = n_ACSIL::s_GraphicsBrush::e_BrushType;
using SCColor = n_ACSIL::s_GraphicsColor;
using SCRect = n_ACSIL::s_GraphicsRectangle;


/*==================================================================================*/
// Shows or hides all native Sierra Chart Reward/Risk drawings.
//
// The drawings remain on the chart and retain their Stop, Entry and Target.
// Only their native visual representation is hidden.
//
static void SetNativeRiskRewardVisibility(
	SCStudyInterfaceRef sc,
	const bool Hide)
{
	std::vector<int> LineNumbers;

	s_UseTool rr;
	rr.Clear();

	int DrawingIndex = 0;

	while (sc.GetUserDrawnChartDrawing(
		sc.ChartNumber,
		DRAWING_UNKNOWN,
		rr,
		DrawingIndex++) != 0)
	{
		if (rr.DrawingType != DRAWING_REWARD_RISK)
			continue;

		// Only modify drawings whose visibility actually needs changing.
		if ((rr.HideDrawing != 0) == Hide)
			continue;

		LineNumbers.push_back(rr.LineNumber);
	}

	// Modify drawings after enumeration so changing a drawing cannot
	// interfere with traversal of the user-drawing list.
	for (const int LineNumber : LineNumbers)
	{
		s_UseTool Tool;
		Tool.Clear();

		// Read the existing drawing and retain ALL original coordinates,
		// especially the third (target) point.  Passing a newly cleared
		// s_UseTool to sc.UseTool() appears to reset the third anchor
		// when HideDrawing is changed on the affected Sierra Chart version.
		// Skip if the drawing is not currently retrievable (for example,
		// while the user is moving it).
		if (sc.GetUserDrawnDrawingByLineNumber(
				sc.ChartNumber, LineNumber, Tool) == 0)
			continue;

		if (Tool.DrawingType != DRAWING_REWARD_RISK)
			continue;

		if ((Tool.HideDrawing != 0) == Hide)
			continue;

		Tool.ChartNumber = sc.ChartNumber;
		Tool.LineNumber = LineNumber;
		Tool.AddAsUserDrawnDrawing = 1;
		Tool.AddMethod = UTAM_ADD_OR_ADJUST;
		Tool.HideDrawing = Hide ? 1 : 0;

		sc.UseTool(Tool);
	}
}


/*==================================================================================*/
// Returns the left and right pixel boundaries of a bar.
//
// Sierra Chart's BarIndexToXPixelCoordinate() returns the horizontal center of a bar.
// For the Risk/Reward overlay we want the rectangle to cover the complete first and
// last bars, so the boundaries are placed halfway between adjacent bar centers.
//
static void GetBarHorizontalEdges(
	SCStudyInterfaceRef sc,
	const int BarIndex,
	int& LeftEdge,
	int& RightEdge)
{
	const int Center =
		sc.BarIndexToXPixelCoordinate(BarIndex);

	// Degenerate fallback. This should only matter on a chart containing one bar.
	if (sc.ArraySize <= 1)
	{
		LeftEdge = Center - 1;
		RightEdge = Center + 1;
		return;
	}

	if (BarIndex > 0)
	{
		const int PreviousCenter =
			sc.BarIndexToXPixelCoordinate(BarIndex - 1);

		LeftEdge =
			(PreviousCenter + Center) / 2;
	}
	else
	{
		const int NextCenter =
			sc.BarIndexToXPixelCoordinate(BarIndex + 1);

		LeftEdge =
			Center - (NextCenter - Center) / 2;
	}

	if (BarIndex < sc.ArraySize - 1)
	{
		const int NextCenter =
			sc.BarIndexToXPixelCoordinate(BarIndex + 1);

		RightEdge =
			(Center + NextCenter) / 2;
	}
	else
	{
		const int PreviousCenter =
			sc.BarIndexToXPixelCoordinate(BarIndex - 1);

		RightEdge =
			Center + (Center - PreviousCenter) / 2;
	}

	// Ensure a non-zero width even at extreme zoom levels.
	if (RightEdge <= LeftEdge)
		RightEdge = LeftEdge + 1;
}


/*==================================================================================*/
// Draws the colored Risk and Reward rectangles.
//
// These overlays are ALWAYS rendered, regardless of whether the native Sierra
// Reward/Risk drawings are currently visible or hidden.
//
static void DrawRiskReward(
	HWND handle,
	HDC context,
	SCStudyInterfaceRef sc)
{
	if (sc.GetUserDrawnDrawingsCount(sc.ChartNumber, 0) == 0)
		return;

	SCRect rect;

	rect.Left = sc.StudyRegionLeftCoordinate;
	rect.Right = sc.StudyRegionRightCoordinate;
	rect.Top = sc.StudyRegionTopCoordinate;
	rect.Bottom = sc.StudyRegionBottomCoordinate;

	sc.Graphics.SetClippingRegionFromRectangle(rect);


	// -------------------------------------------------------------------------
	// Colors
	// -------------------------------------------------------------------------

	SCColor reward_color;
	SCPen reward_pen;
	SCBrush reward_brush;

	SCColor risk_color;
	SCPen risk_pen;
	SCBrush risk_brush;

	reward_color.SetColorValue(sc.Input[0].GetColor());
	risk_color.SetColorValue(sc.Input[1].GetColor());


	// -------------------------------------------------------------------------
	// No border around the rectangles
	// -------------------------------------------------------------------------

	reward_pen.m_PenStyle = SCPenStyle::PEN_STYLE_NULL;
	risk_pen.m_PenStyle = SCPenStyle::PEN_STYLE_NULL;


	// -------------------------------------------------------------------------
	// Transparency
	// -------------------------------------------------------------------------

	const auto alpha =
		static_cast<uint8_t>(
			sc.Input[2].GetInt() * 255 * 0.01);

	reward_color.Color.RGB.Alpha = alpha;
	risk_color.Color.RGB.Alpha = alpha;

	reward_brush.m_BrushType = SCBrushType::BRUSH_TYPE_SOLID;
	reward_brush.m_BrushColor = reward_color;

	risk_brush.m_BrushType = SCBrushType::BRUSH_TYPE_SOLID;
	risk_brush.m_BrushColor = risk_color;


	// -------------------------------------------------------------------------
	// Visible chart range
	// -------------------------------------------------------------------------

	t_ChartArrayDataType highest = 0.0;
	t_ChartArrayDataType lowest = 0.0;

	sc.GetMainGraphVisibleHighAndLow(highest, lowest);


	// -------------------------------------------------------------------------
	// Process all Reward/Risk drawings
	// -------------------------------------------------------------------------

	s_UseTool rr;
	rr.Clear();

	int DrawingIndex = 0;

	while (sc.GetUserDrawnChartDrawing(
		sc.ChartNumber,
		DRAWING_UNKNOWN,
		rr,
		DrawingIndex++) != 0)
	{
		if (rr.DrawingType != DRAWING_REWARD_RISK)
			continue;


		const int iMin =
			min(rr.BeginIndex,
				min(rr.EndIndex, rr.ThirdIndex));

		const int iMax =
			max(rr.BeginIndex,
				max(rr.EndIndex, rr.ThirdIndex));

		const float pMin =
			min(rr.BeginValue,
				min(rr.EndValue, rr.ThirdValue));

		const float pMax =
			max(rr.BeginValue,
				max(rr.EndValue, rr.ThirdValue));


		// Skip drawings outside the visible horizontal range.
		if (iMax < sc.IndexOfFirstVisibleBar
			|| iMin > sc.IndexOfLastVisibleBar)
			continue;

		// Skip drawings outside the visible vertical range.
		if (pMax < lowest
			|| pMin > highest)
			continue;


		// ---------------------------------------------------------------------
		// Convert drawing coordinates to pixels
		// ---------------------------------------------------------------------

		int xLeft = 0;
		int xRightUnused = 0;

		GetBarHorizontalEdges(
			sc,
			iMin,
			xLeft,
			xRightUnused);

		int xLeftUnused = 0;
		int xRight = 0;

		GetBarHorizontalEdges(
			sc,
			iMax,
			xLeftUnused,
			xRight);

		const int yStop =
			sc.RegionValueToYPixelCoordinate(
				rr.BeginValue,
				sc.GraphRegion);

		const int yEntry =
			sc.RegionValueToYPixelCoordinate(
				rr.EndValue,
				sc.GraphRegion);

		const int yTarget =
			sc.RegionValueToYPixelCoordinate(
				rr.ThirdValue,
				sc.GraphRegion);


		// ---------------------------------------------------------------------
		// Risk area: Stop <-> Entry
		// ---------------------------------------------------------------------

		sc.Graphics.SetBrush(risk_brush);
		sc.Graphics.SetPen(risk_pen);

		sc.Graphics.DrawRectangle(
			xLeft,
			max(yStop, yEntry),
			xRight - 1,
			min(yStop, yEntry) + 1);


		// ---------------------------------------------------------------------
		// Reward area: Entry <-> Target
		// ---------------------------------------------------------------------

		sc.Graphics.SetBrush(reward_brush);
		sc.Graphics.SetPen(reward_pen);

		sc.Graphics.DrawRectangle(
			xLeft,
			max(yEntry, yTarget),
			xRight - 1,
			min(yEntry, yTarget) + 1);
	}


	sc.Graphics.ResetPen();
	sc.Graphics.ResetBrush();
	sc.Graphics.ResetClippingRegion();
}


/*==================================================================================*/
SCSFExport scsf_MKLRiskReward(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "MKL Risk Reward";

		sc.GraphRegion = 0;
		sc.AutoLoop = 0;
		sc.ValueFormat = VALUEFORMAT_INHERITED;
		sc.ScaleRangeType = SCALE_SAMEASREGION;

		sc.GlobalDisplayStudySubgraphsNameAndValue = 0;
		sc.DisplayStudyInputValues = 0;
		sc.HideDLLAndFunctionNames = 1;

		sc.UpdateAlways = 1;


		// ---------------------------------------------------------------------
		// Inputs
		// ---------------------------------------------------------------------

		sc.Input[0].Name = "Color: Reward";
		sc.Input[0].SetColor(COLOR_LIME);

		sc.Input[1].Name = "Color: Risk";
		sc.Input[1].SetColor(COLOR_RED);

		sc.Input[2].Name = "Transparency";
		sc.Input[2].SetInt(80);
		sc.Input[2].SetIntLimits(0, 100);

		// This controls ONLY the visibility of Sierra Chart's original
		// Reward/Risk drawings. The MKL colored overlays always remain visible.
		sc.Input[3].Name = "Show Native Risk/Reward Tool";
		sc.Input[3].SetYesNo(0);

		// Select one of Sierra Chart's ACS Control Bar buttons (1-150).
		// 0 disables Control Bar integration.
		sc.Input[4].Name = "Control Bar Button Number (0 = Disabled)";
		sc.Input[4].SetInt(0);
		sc.Input[4].SetIntLimits(0, 150);

		return;
	}


	// Remember which button number was previously configured so that changing
	// the Study Input cannot leave the old button in an enabled state.
	int& LastControlBarButtonNumber = sc.GetPersistentIntFast(0);


	// -------------------------------------------------------------------------
	// Study is being removed.
	//
	// Restore all native Reward/Risk drawings so the user's drawings are
	// never left hidden after removing the study. Also release the Control Bar
	// button's On state.
	// -------------------------------------------------------------------------

	if (sc.LastCallToFunction)
	{
		SetNativeRiskRewardVisibility(sc, false);

		if (LastControlBarButtonNumber != 0)
			sc.SetCustomStudyControlBarButtonEnable(
				LastControlBarButtonNumber,
				0);

		sc.p_GDIFunction = nullptr;

		return;
	}


	// -------------------------------------------------------------------------
	// Install graphics renderer.
	//
	// Important: The renderer is NOT tied to native-tool visibility. Therefore
	// the green/red overlays remain visible in both states.
	// -------------------------------------------------------------------------

	if (sc.UpdateStartIndex == 0)
		sc.p_GDIFunction = DrawRiskReward;


	// -------------------------------------------------------------------------
	// Control Bar button configuration
	// -------------------------------------------------------------------------

	const int ControlBarButtonNumber =
		sc.Input[4].GetInt();

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
		// Button ON  = native Reward/Risk drawings visible.
		// Button OFF = native Reward/Risk drawings hidden.
		if (sc.MenuEventID == ControlBarButtonNumber)
		{
			if (sc.PointerEventType == SC_ACS_BUTTON_ON)
				sc.Input[3].SetYesNo(1);
			else if (sc.PointerEventType == SC_ACS_BUTTON_OFF)
				sc.Input[3].SetYesNo(0);
		}
	}


	// -------------------------------------------------------------------------
	// Native Reward/Risk visibility
	// -------------------------------------------------------------------------

	const bool ShowNativeRiskRewardTool =
		sc.Input[3].GetYesNo() != 0;

	SetNativeRiskRewardVisibility(
		sc,
		!ShowNativeRiskRewardTool);


	// -------------------------------------------------------------------------
	// Keep the Control Bar button visually synchronized with the Study Input.
	// This also means changing the Input manually in Study Settings updates the
	// button state immediately after Apply/OK.
	// -------------------------------------------------------------------------

	if (ControlBarButtonNumber != 0)
	{
		const int CurrentButtonState =
			sc.GetCustomStudyControlBarButtonEnableState(
				ControlBarButtonNumber);

		const int DesiredButtonState =
			ShowNativeRiskRewardTool ? 1 : 0;

		if (CurrentButtonState != DesiredButtonState)
		{
			sc.SetCustomStudyControlBarButtonEnable(
				ControlBarButtonNumber,
				DesiredButtonState);
		}
	}
}
