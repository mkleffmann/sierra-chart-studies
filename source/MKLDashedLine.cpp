// MKLDashedLine.cpp

#include "sierrachart.h"

#include <cmath>
#include <cstdint>

SCDLLName("MKL DashedLine");

namespace
{
    constexpr double EPSILON = 1.0e-8;

    bool IsUsableStudyValue(const float Value)
    {
        // Sierra Chart commonly uses zero to indicate that a study has no
        // value at a particular bar.
        return Value != 0.0f && std::isfinite(static_cast<double>(Value));
    }

    bool ClipTest(
        const double P,
        const double Q,
        double& EnteringParameter,
        double& LeavingParameter)
    {
        if (std::fabs(P) < EPSILON)
            return Q >= 0.0;

        const double Ratio = Q / P;

        if (P < 0.0)
        {
            if (Ratio > LeavingParameter)
                return false;

            if (Ratio > EnteringParameter)
                EnteringParameter = Ratio;
        }
        else
        {
            if (Ratio < EnteringParameter)
                return false;

            if (Ratio < LeavingParameter)
                LeavingParameter = Ratio;
        }

        return true;
    }

    // Liang-Barsky clipping keeps the custom graphics inside the study region.
    bool ClipLineToStudyRegion(
        SCStudyInterfaceRef SC,
        double& X0,
        double& Y0,
        double& X1,
        double& Y1)
    {
        const double Left = static_cast<double>(SC.StudyRegionLeftCoordinate);
        const double Top = static_cast<double>(SC.StudyRegionTopCoordinate);
        const double Right = static_cast<double>(SC.StudyRegionRightCoordinate);
        const double Bottom = static_cast<double>(SC.StudyRegionBottomCoordinate);

        const double DeltaX = X1 - X0;
        const double DeltaY = Y1 - Y0;
        double EnteringParameter = 0.0;
        double LeavingParameter = 1.0;

        if (!ClipTest(-DeltaX, X0 - Left, EnteringParameter, LeavingParameter)
            || !ClipTest(DeltaX, Right - X0, EnteringParameter, LeavingParameter)
            || !ClipTest(-DeltaY, Y0 - Top, EnteringParameter, LeavingParameter)
            || !ClipTest(DeltaY, Bottom - Y0, EnteringParameter, LeavingParameter))
        {
            return false;
        }

        const double OriginalX0 = X0;
        const double OriginalY0 = Y0;

        X0 = OriginalX0 + EnteringParameter * DeltaX;
        Y0 = OriginalY0 + EnteringParameter * DeltaY;
        X1 = OriginalX0 + LeavingParameter * DeltaX;
        Y1 = OriginalY0 + LeavingParameter * DeltaY;

        return true;
    }

    void DrawSolidSegment(
        SCStudyInterfaceRef SC,
        double X0,
        double Y0,
        double X1,
        double Y1,
        const uint32_t Color,
        const uint32_t LineWidth)
    {
        if (!ClipLineToStudyRegion(SC, X0, Y0, X1, Y1))
            return;

        const int StartX = static_cast<int>(std::lround(X0));
        const int StartY = static_cast<int>(std::lround(Y0));
        const int EndX = static_cast<int>(std::lround(X1));
        const int EndY = static_cast<int>(std::lround(Y1));

        if (StartX == EndX && StartY == EndY)
            return;

        SC.DrawGraphics_MoveTo(StartX, StartY);
        SC.DrawGraphics_LineTo(
            EndX,
            EndY,
            Color,
            LineWidth,
            LINESTYLE_SOLID);
    }

    void DrawFixedPixelDashedPolyline(
        SCStudyInterfaceRef SC,
        SCFloatArrayRef Values,
        const uint32_t Color,
        const uint32_t LineWidth,
        const double DashLength,
        const double GapLength)
    {
        const int ValueCount = Values.GetArraySize();
        if (ValueCount < 2 || SC.ArraySize < 2)
            return;

        int FirstIndex = SC.IndexOfFirstVisibleBar - 1;
        if (FirstIndex < 0)
            FirstIndex = 0;

        int LastIndex = SC.IndexOfLastVisibleBar + 1;
        if (LastIndex >= SC.ArraySize)
            LastIndex = SC.ArraySize - 1;
        if (LastIndex >= ValueCount)
            LastIndex = ValueCount - 1;

        if (FirstIndex >= LastIndex)
            return;

        const double PatternLength = DashLength + GapLength;
        double PatternPosition = 0.0;
        double PreviousX = 0.0;
        double PreviousY = 0.0;
        bool HavePreviousPoint = false;

        for (int Index = FirstIndex; Index <= LastIndex; ++Index)
        {
            const float Value = Values[Index];

            if (!IsUsableStudyValue(Value))
            {
                // Do not connect separate sessions or data blocks through a
                // missing source value.
                HavePreviousPoint = false;
                PatternPosition = 0.0;
                continue;
            }

            const double CurrentX = static_cast<double>(
                SC.BarIndexToXPixelCoordinate(Index));
            const double CurrentY = static_cast<double>(
                SC.RegionValueToYPixelCoordinate(Value, SC.GraphRegion));

            if (!HavePreviousPoint)
            {
                PreviousX = CurrentX;
                PreviousY = CurrentY;
                HavePreviousPoint = true;
                continue;
            }

            const double DeltaX = CurrentX - PreviousX;
            const double DeltaY = CurrentY - PreviousY;
            const double SegmentLength = std::hypot(DeltaX, DeltaY);

            if (SegmentLength > EPSILON)
            {
                double DistanceAlongSegment = 0.0;

                while (DistanceAlongSegment < SegmentLength - EPSILON)
                {
                    const bool DrawCurrentPart = PatternPosition < DashLength;
                    const double DistanceToBoundary = DrawCurrentPart
                        ? DashLength - PatternPosition
                        : PatternLength - PatternPosition;
                    const double RemainingSegmentLength =
                        SegmentLength - DistanceAlongSegment;
                    const double Step = DistanceToBoundary < RemainingSegmentLength
                        ? DistanceToBoundary
                        : RemainingSegmentLength;

                    if (Step <= EPSILON)
                    {
                        PatternPosition = DrawCurrentPart ? DashLength : 0.0;
                        continue;
                    }

                    if (DrawCurrentPart)
                    {
                        const double StartFraction =
                            DistanceAlongSegment / SegmentLength;
                        const double EndFraction =
                            (DistanceAlongSegment + Step) / SegmentLength;

                        DrawSolidSegment(
                            SC,
                            PreviousX + DeltaX * StartFraction,
                            PreviousY + DeltaY * StartFraction,
                            PreviousX + DeltaX * EndFraction,
                            PreviousY + DeltaY * EndFraction,
                            Color,
                            LineWidth);
                    }

                    DistanceAlongSegment += Step;
                    PatternPosition += Step;

                    if (PatternPosition >= PatternLength - EPSILON)
                        PatternPosition = 0.0;
                }
            }

            PreviousX = CurrentX;
            PreviousY = CurrentY;
        }
    }
}

void DrawFixedPixelDashedLineToChart(
    HWND WindowHandle,
    HDC DeviceContext,
    SCStudyInterfaceRef SC);

SCSFExport scsf_MKLDashedLine(SCStudyInterfaceRef SC)
{
    SCSubgraphRef DashedLine = SC.Subgraph[0];

    SCInputRef SourceStudyAndSubgraph = SC.Input[0];
    SCInputRef DashLength = SC.Input[1];
    SCInputRef GapLength = SC.Input[2];
    SCInputRef LineWidth = SC.Input[3];

    if (SC.SetDefaults)
    {
        SC.GraphName = "MKL DashedLine";
        SC.StudyDescription =
            "Redraws one selected study Subgraph with a dash pattern measured "
            "in screen pixels. Add the study more than once to draw multiple "
            "source Subgraphs. The line width can be inherited from the "
            "source Subgraph or overridden in the study settings.";
        SC.GraphRegion = 0;
        SC.AutoLoop = 0;
        SC.CalculationPrecedence = VERY_LOW_PREC_LEVEL;
        SC.HideDLLAndFunctionNames = 1;
        SC.UpdateAlways = 1;

        DashedLine.Name = "Dashed Line";
        DashedLine.DrawStyle = DRAWSTYLE_IGNORE;
        DashedLine.PrimaryColor = RGB(110, 125, 190);
        DashedLine.LineWidth = 2;
        DashedLine.DrawZeros = 0;

        SourceStudyAndSubgraph.Name = "Source Study and Subgraph";
        SourceStudyAndSubgraph.SetStudySubgraphValues(1, 0);

        DashLength.Name = "Dash Length in Pixels";
        DashLength.SetInt(8);
        DashLength.SetIntLimits(1, 100);

        GapLength.Name = "Gap Length in Pixels";
        GapLength.SetInt(5);
        GapLength.SetIntLimits(1, 100);

        LineWidth.Name = "Line Width (0 = Use Source Study)";
        LineWidth.SetInt(0);
        LineWidth.SetIntLimits(0, 20);

        return;
    }

    // Refresh the callback address after the DLL is rebuilt and reloaded.
    SC.p_GDIFunction = DrawFixedPixelDashedLineToChart;

    if (SC.LastCallToFunction)
        return;

    SCFloatArray SourceValues;
    SC.GetStudyArrayUsingID(
        SourceStudyAndSubgraph.GetStudyID(),
        SourceStudyAndSubgraph.GetSubgraphIndex(),
        SourceValues);

    const int SourceValueCount = SourceValues.GetArraySize();

    int StartIndex = SC.UpdateStartIndex;
    if (StartIndex < 0)
        StartIndex = 0;
    if (StartIndex > 0)
        --StartIndex;

    for (int Index = StartIndex; Index < SC.ArraySize; ++Index)
    {
        DashedLine[Index] = Index < SourceValueCount
            ? SourceValues[Index]
            : 0.0f;
    }
}

void DrawFixedPixelDashedLineToChart(
    HWND WindowHandle,
    HDC DeviceContext,
    SCStudyInterfaceRef SC)
{
    (void)WindowHandle;
    (void)DeviceContext;

    if (SC.HideStudy || SC.ArraySize < 2)
        return;

    int DashLength = SC.Input[1].GetInt();
    if (DashLength < 1)
        DashLength = 1;

    int GapLength = SC.Input[2].GetInt();
    if (GapLength < 1)
        GapLength = 1;

    int ConfiguredLineWidth = SC.Input[3].GetInt();
    if (ConfiguredLineWidth < 0)
        ConfiguredLineWidth = 0;

    const int32_t SourceStudyID =
        static_cast<int32_t>(SC.Input[0].GetStudyID());
    const int32_t SourceSubgraphIndex =
        static_cast<int32_t>(SC.Input[0].GetSubgraphIndex());

    // Use this study's Subgraph settings only as fallbacks. The color and,
    // unless overridden, the width are taken from the source Subgraph.
    uint32_t LineColor =
        static_cast<uint32_t>(SC.Subgraph[0].PrimaryColor);
    uint32_t SourcePrimaryColor = 0;
    uint32_t SourceSecondaryColor = 0;
    uint32_t SourceSecondaryColorUsed = 0;

    if (SC.GetStudySubgraphColors(
            SC.ChartNumber,
            SourceStudyID,
            SourceSubgraphIndex,
            SourcePrimaryColor,
            SourceSecondaryColor,
            SourceSecondaryColorUsed) != 0)
    {
        LineColor = SourcePrimaryColor;
    }

    int32_t ActualLineWidth =
        static_cast<int32_t>(SC.Subgraph[0].LineWidth);
    int32_t SourceLineWidth = 0;

    if (ConfiguredLineWidth > 0)
    {
        ActualLineWidth = ConfiguredLineWidth;
    }
    else if (SC.GetStudySubgraphLineWidth(
                 SC.ChartNumber,
                 SourceStudyID,
                 SourceSubgraphIndex,
                 SourceLineWidth) != 0
        && SourceLineWidth > 0)
    {
        ActualLineWidth = SourceLineWidth;
    }

    if (ActualLineWidth < 1)
        ActualLineWidth = 1;

    DrawFixedPixelDashedPolyline(
        SC,
        SC.Subgraph[0].Data,
        LineColor,
        static_cast<uint32_t>(ActualLineWidth),
        static_cast<double>(DashLength),
        static_cast<double>(GapLength));
}
