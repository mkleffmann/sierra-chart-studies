// MKLCurrentTradeFills.cpp

#include "sierrachart.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

SCDLLName("MKL Current Trade Fills");

namespace
{
    constexpr double EPSILON = 1.0e-8;

    // Defaults match the original study. The active colors are user-configurable
    // in the Study Settings (Inputs) tab, not fixed at runtime.
    const uint32_t DEFAULT_LONG_ENTRY_COLOR = RGB(0, 255, 0);
    const uint32_t DEFAULT_LONG_EXIT_COLOR = RGB(0, 155, 0);
    const uint32_t DEFAULT_SHORT_ENTRY_COLOR = RGB(255, 0, 0);
    const uint32_t DEFAULT_SHORT_EXIT_COLOR = RGB(157, 0, 0);
    const uint32_t DEFAULT_PROFIT_CONNECTION_COLOR = RGB(100, 180, 100);
    const uint32_t DEFAULT_LOSS_CONNECTION_COLOR = RGB(180, 100, 100);

    struct FillColors
    {
        uint32_t LongEntry;
        uint32_t LongExit;
        uint32_t ShortEntry;
        uint32_t ShortExit;
        uint32_t ProfitConnection;
        uint32_t LossConnection;
    };

    constexpr uint32_t CONNECTING_LINE_WIDTH = 2;

    enum class FillRole
    {
        LongEntry,
        LongExit,
        ShortEntry,
        ShortExit
    };

    enum class MarkerShape
    {
        Triangle = 0,
        HorizontalLine = 1,
        Point = 2
    };

    struct CurrentFill
    {
        s_SCOrderFillData FillData;
        uint32_t EffectiveQuantity = 0;
        // FIFO quantity from this entry that has not yet been matched to an exit.
        uint32_t RemainingOpenQuantity = 0;
        FillRole Role = FillRole::LongEntry;
        int BarIndex = -1;
        int X = 0;
        int Y = 0;
    };

    struct OpenLot
    {
        int FillIndex = -1;
        uint32_t RemainingQuantity = 0;
        int Direction = 0;
    };

    struct FillConnection
    {
        int EntryFillIndex = -1;
        int ExitFillIndex = -1;
        bool IsProfitable = false;
    };

    long long AbsoluteLongLong(const long long Value)
    {
        return Value < 0 ? -Value : Value;
    }

    int GetFillDirection(const s_SCOrderFillData& FillData)
    {
        if (FillData.BuySell == BSE_BUY)
            return 1;

        if (FillData.BuySell == BSE_SELL)
            return -1;

        return 0;
    }

    bool HasReachedPositionQuantity(
        const long long AccumulatedQuantity,
        const long long PositionQuantity)
    {
        if (PositionQuantity > 0)
            return AccumulatedQuantity >= PositionQuantity;

        if (PositionQuantity < 0)
            return AccumulatedQuantity <= PositionQuantity;

        return true;
    }

    bool GetCurrentPositionFills(
        SCStudyInterfaceRef SC,
        std::vector<CurrentFill>& CurrentFills)
    {
        CurrentFills.clear();

        s_SCPositionData PositionData;
        if (SC.GetTradePosition(PositionData) != 1)
            return false;

        const long long PositionQuantity = static_cast<long long>(
            std::llround(PositionData.PositionQuantity));

        if (PositionQuantity == 0)
            return false;

        const int FillCount = SC.GetOrderFillArraySize();
        if (FillCount <= 0)
            return false;

        long long AccumulatedQuantity = 0;
        bool FoundBeginningOfCurrentPosition = false;

        // Sierra Chart documents this reverse-iteration method for obtaining
        // all order fills which belong to the currently open Position.
        for (int FillIndex = FillCount - 1; FillIndex >= 0; --FillIndex)
        {
            s_SCOrderFillData FillData;
            if (SC.GetOrderFillEntry(
                    static_cast<unsigned int>(FillIndex),
                    FillData) == 0)
            {
                continue;
            }

            const int Direction = GetFillDirection(FillData);
            if (Direction == 0 || FillData.Quantity == 0)
                continue;

            CurrentFill Fill;
            Fill.FillData = FillData;
            Fill.EffectiveQuantity =
                static_cast<uint32_t>(FillData.Quantity);

            CurrentFills.push_back(Fill);

            AccumulatedQuantity +=
                static_cast<long long>(Direction)
                * static_cast<long long>(FillData.Quantity);

            if (!HasReachedPositionQuantity(
                    AccumulatedQuantity,
                    PositionQuantity))
            {
                continue;
            }

            // A single fill can reverse a Position. In that case, only the
            // portion beyond flat belongs to the currently open trade.
            const long long ExcessQuantity = AbsoluteLongLong(
                AccumulatedQuantity - PositionQuantity);

            if (ExcessQuantity > 0)
            {
                const long long OriginalQuantity =
                    static_cast<long long>(
                        CurrentFills.back().EffectiveQuantity);

                if (ExcessQuantity >= OriginalQuantity)
                {
                    CurrentFills.clear();
                    return false;
                }

                CurrentFills.back().EffectiveQuantity =
                    static_cast<uint32_t>(
                        OriginalQuantity - ExcessQuantity);
            }

            FoundBeginningOfCurrentPosition = true;
            break;
        }

        if (!FoundBeginningOfCurrentPosition)
        {
            // Do not display a potentially incorrect subset when the chart
            // does not have enough fill history to reconstruct the Position.
            CurrentFills.clear();
            return false;
        }

        std::reverse(CurrentFills.begin(), CurrentFills.end());
        return !CurrentFills.empty();
    }

    int FindBarStartingAtOrBeforeDateTime(
        SCStudyInterfaceRef SC,
        const SCDateTime& DateTime)
    {
        if (SC.ArraySize <= 0
            || DateTime < SC.BaseDateTimeIn[0])
        {
            return -1;
        }

        // Find the first chart bar whose start is later than DateTime. The
        // preceding bar is therefore the last bar that had already started
        // when the fill occurred. This also handles gaps between the actual
        // end of one bar and the nominal start of the next bar.
        int First = 0;
        int Last = SC.ArraySize;

        while (First < Last)
        {
            const int Middle = First + (Last - First) / 2;

            if (DateTime < SC.BaseDateTimeIn[Middle])
                Last = Middle;
            else
                First = Middle + 1;
        }

        return First - 1;
    }

    bool AssignPixelCoordinates(
        SCStudyInterfaceRef SC,
        std::vector<CurrentFill>& CurrentFills)
    {
        if (SC.ArraySize <= 0)
            return false;

        for (CurrentFill& Fill : CurrentFills)
        {
            // Assign the fill to the latest chart bar whose start is not
            // later than the fill. Nearest matching can round forward, while
            // containing matching can choose the next bar when the fill time
            // falls into a gap in the chart data.
            Fill.BarIndex = FindBarStartingAtOrBeforeDateTime(
                SC,
                Fill.FillData.FillDateTime);

            if (Fill.BarIndex < 0 || Fill.BarIndex >= SC.ArraySize)
                return false;

            Fill.X = SC.BarIndexToXPixelCoordinate(Fill.BarIndex);
            Fill.Y = SC.RegionValueToYPixelCoordinate(
                Fill.FillData.FillPrice,
                SC.GraphRegion);
        }

        return true;
    }

    void BuildFillRolesAndConnections(
        std::vector<CurrentFill>& CurrentFills,
        std::vector<FillConnection>& Connections)
    {
        Connections.clear();

        std::deque<OpenLot> OpenLots;
        int OpenDirection = 0;

        for (int FillIndex = 0;
             FillIndex < static_cast<int>(CurrentFills.size());
             ++FillIndex)
        {
            CurrentFill& Fill = CurrentFills[FillIndex];
            const int FillDirection = GetFillDirection(Fill.FillData);
            uint32_t RemainingQuantity = Fill.EffectiveQuantity;

            if (OpenDirection == 0 || OpenDirection == FillDirection)
            {
                Fill.Role = FillDirection > 0
                    ? FillRole::LongEntry
                    : FillRole::ShortEntry;

                OpenLot Lot;
                Lot.FillIndex = FillIndex;
                Lot.RemainingQuantity = RemainingQuantity;
                Lot.Direction = FillDirection;
                OpenLots.push_back(Lot);
                Fill.RemainingOpenQuantity = RemainingQuantity;
                OpenDirection = FillDirection;
                continue;
            }

            Fill.Role = FillDirection > 0
                ? FillRole::ShortExit
                : FillRole::LongExit;

            // Match exits against entries FIFO, which is Sierra Chart's
            // standard fill-matching convention for connecting lines.
            while (RemainingQuantity > 0 && !OpenLots.empty())
            {
                OpenLot& EntryLot = OpenLots.front();
                const uint32_t MatchedQuantity =
                    RemainingQuantity < EntryLot.RemainingQuantity
                    ? RemainingQuantity
                    : EntryLot.RemainingQuantity;

                if (Connections.empty()
                    || Connections.back().EntryFillIndex
                        != EntryLot.FillIndex
                    || Connections.back().ExitFillIndex != FillIndex)
                {
                    FillConnection Connection;
                    Connection.EntryFillIndex = EntryLot.FillIndex;
                    Connection.ExitFillIndex = FillIndex;

                    const CurrentFill& EntryFill =
                        CurrentFills[EntryLot.FillIndex];
                    const double PriceResult =
                        (Fill.FillData.FillPrice
                            - EntryFill.FillData.FillPrice)
                        * static_cast<double>(EntryLot.Direction);

                    // As with Sierra Chart's primary/secondary connection
                    // colors, a non-losing match uses the primary color.
                    Connection.IsProfitable = PriceResult >= -EPSILON;
                    Connections.push_back(Connection);
                }

                RemainingQuantity -= MatchedQuantity;
                EntryLot.RemainingQuantity -= MatchedQuantity;
                CurrentFills[EntryLot.FillIndex].RemainingOpenQuantity -=
                    MatchedQuantity;

                if (EntryLot.RemainingQuantity == 0)
                    OpenLots.pop_front();
            }

            if (OpenLots.empty())
                OpenDirection = 0;

            // This is only expected for a Position reversal. Treat the
            // quantity beyond flat as the entry into the new Position.
            if (RemainingQuantity > 0)
            {
                Fill.Role = FillDirection > 0
                    ? FillRole::LongEntry
                    : FillRole::ShortEntry;

                OpenLot Lot;
                Lot.FillIndex = FillIndex;
                Lot.RemainingQuantity = RemainingQuantity;
                Lot.Direction = FillDirection;
                OpenLots.push_back(Lot);
                Fill.RemainingOpenQuantity = RemainingQuantity;
                OpenDirection = FillDirection;
            }
        }
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

    bool ClipLineToStudyRegion(
        SCStudyInterfaceRef SC,
        double& X0,
        double& Y0,
        double& X1,
        double& Y1)
    {
        const double Left =
            static_cast<double>(SC.StudyRegionLeftCoordinate);
        const double Top =
            static_cast<double>(SC.StudyRegionTopCoordinate);
        const double Right =
            static_cast<double>(SC.StudyRegionRightCoordinate);
        const double Bottom =
            static_cast<double>(SC.StudyRegionBottomCoordinate);

        const double DeltaX = X1 - X0;
        const double DeltaY = Y1 - Y0;
        double EnteringParameter = 0.0;
        double LeavingParameter = 1.0;

        if (!ClipTest(
                -DeltaX,
                X0 - Left,
                EnteringParameter,
                LeavingParameter)
            || !ClipTest(
                DeltaX,
                Right - X0,
                EnteringParameter,
                LeavingParameter)
            || !ClipTest(
                -DeltaY,
                Y0 - Top,
                EnteringParameter,
                LeavingParameter)
            || !ClipTest(
                DeltaY,
                Bottom - Y0,
                EnteringParameter,
                LeavingParameter))
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

    void DrawLine(
        SCStudyInterfaceRef SC,
        double X0,
        double Y0,
        double X1,
        double Y1,
        const uint32_t Color,
        uint32_t LineWidth,
        const SubgraphLineStyles LineStyle)
    {
        if (!ClipLineToStudyRegion(SC, X0, Y0, X1, Y1))
            return;

        if (LineWidth < 1)
            LineWidth = 1;

        SC.DrawGraphics_MoveTo(
            static_cast<int>(std::lround(X0)),
            static_cast<int>(std::lround(Y0)));

        SC.DrawGraphics_LineTo(
            static_cast<int>(std::lround(X1)),
            static_cast<int>(std::lround(Y1)),
            Color,
            LineWidth,
            LineStyle);
    }

    uint32_t GetFillColor(const FillRole Role, const FillColors& Colors)
    {
        switch (Role)
        {
            case FillRole::LongEntry:
                return Colors.LongEntry;

            case FillRole::LongExit:
                return Colors.LongExit;

            case FillRole::ShortEntry:
                return Colors.ShortEntry;

            case FillRole::ShortExit:
                return Colors.ShortExit;
        }

        return Colors.LongEntry;
    }

    bool IsEntryRole(const FillRole Role)
    {
        return Role == FillRole::LongEntry
            || Role == FillRole::ShortEntry;
    }

    int GetTriangleHalfWidth(const int MarkerSize)
    {
        // For an equilateral triangle with a vertical side length of
        // 2 * MarkerSize, the horizontal altitude is
        // sqrt(3) * MarkerSize. Half of that altitude is used here because
        // CenterX lies midway between the vertical base and the tip.
        int HalfWidth = static_cast<int>(std::lround(
            0.8660254037844386 * static_cast<double>(MarkerSize)));

        if (HalfWidth < 1)
            HalfWidth = 1;

        return HalfWidth;
    }

    int GetMarkerCenterX(
        const CurrentFill& Fill,
        const int MarkerSize,
        const MarkerShape Shape)
    {
        const bool IsEntry = IsEntryRole(Fill.Role);

        // For a triangle, shift its center by half of the horizontal altitude
        // so the tip, rather than the triangle center, sits on the chart bar
        // center.
        const int HorizontalOffset =
            Shape == MarkerShape::Triangle
            ? GetTriangleHalfWidth(MarkerSize)
            : 0;

        return Fill.X
            + (IsEntry ? -HorizontalOffset : HorizontalOffset);
    }

    int GetMarkerConnectionX(
        const CurrentFill& Fill,
        const int MarkerSize,
        const MarkerShape Shape)
    {
        const int CenterX = GetMarkerCenterX(
            Fill,
            MarkerSize,
            Shape);

        if (Shape != MarkerShape::Triangle)
            return CenterX;

        // Connect to the triangle tip, which is exactly the center of the
        // corresponding chart bar.
        const int TriangleHalfWidth =
            GetTriangleHalfWidth(MarkerSize);

        return CenterX
            + (IsEntryRole(Fill.Role)
                ? TriangleHalfWidth
                : -TriangleHalfWidth);
    }

    void DrawFilledTriangle(
        SCStudyInterfaceRef SC,
        const CurrentFill& Fill,
        const int CenterX,
        const int MarkerSize,
        const uint32_t Color)
    {
        const bool PointsRight = IsEntryRole(Fill.Role);
        const int TriangleHalfWidth =
            GetTriangleHalfWidth(MarkerSize);
        const double HorizontalSpan =
            2.0 * static_cast<double>(TriangleHalfWidth);

        for (int OffsetY = -MarkerSize;
             OffsetY <= MarkerSize;
             ++OffsetY)
        {
            const int AbsoluteOffsetY =
                OffsetY < 0 ? -OffsetY : OffsetY;
            const double Taper = HorizontalSpan
                * static_cast<double>(AbsoluteOffsetY)
                / static_cast<double>(MarkerSize);

            double StartX = 0.0;
            double EndX = 0.0;

            if (PointsRight)
            {
                StartX = CenterX - TriangleHalfWidth;
                EndX = CenterX + TriangleHalfWidth - Taper;
            }
            else
            {
                StartX = CenterX - TriangleHalfWidth + Taper;
                EndX = CenterX + TriangleHalfWidth;
            }

            DrawLine(
                SC,
                StartX,
                Fill.Y + OffsetY,
                EndX,
                Fill.Y + OffsetY,
                Color,
                1,
                LINESTYLE_SOLID);
        }
    }

    void DrawHorizontalMarker(
        SCStudyInterfaceRef SC,
        const CurrentFill& Fill,
        const int CenterX,
        const int MarkerSize,
        const uint32_t Color)
    {
        DrawLine(
            SC,
            CenterX - MarkerSize,
            Fill.Y,
            CenterX + MarkerSize,
            Fill.Y,
            Color,
            3,
            LINESTYLE_SOLID);
    }

    void DrawPointMarker(
        SCStudyInterfaceRef SC,
        const CurrentFill& Fill,
        const int CenterX,
        const int MarkerSize,
        const uint32_t Color)
    {
        const int RadiusSquared = MarkerSize * MarkerSize;

        // Draw a filled circle using horizontal pixel rows. This avoids a
        // renderer-specific brush path and behaves the same with OpenGL.
        for (int OffsetY = -MarkerSize;
             OffsetY <= MarkerSize;
             ++OffsetY)
        {
            const int RemainingSquared =
                RadiusSquared - OffsetY * OffsetY;
            const int HalfWidth = static_cast<int>(
                std::floor(std::sqrt(
                    static_cast<double>(RemainingSquared))));

            DrawLine(
                SC,
                CenterX - HalfWidth,
                Fill.Y + OffsetY,
                CenterX + HalfWidth,
                Fill.Y + OffsetY,
                Color,
                1,
                LINESTYLE_SOLID);
        }
    }

    void DrawFillMarker(
        SCStudyInterfaceRef SC,
        const CurrentFill& Fill,
        int MarkerSize,
        const MarkerShape Shape,
        const FillColors& Colors)
    {
        if (MarkerSize < 2)
            MarkerSize = 2;

        const int CenterX = GetMarkerCenterX(
            Fill,
            MarkerSize,
            Shape);
        const uint32_t Color = GetFillColor(Fill.Role, Colors);

        switch (Shape)
        {
            case MarkerShape::Triangle:
                DrawFilledTriangle(
                    SC,
                    Fill,
                    CenterX,
                    MarkerSize,
                    Color);
                break;

            case MarkerShape::HorizontalLine:
                DrawHorizontalMarker(
                    SC,
                    Fill,
                    CenterX,
                    MarkerSize,
                    Color);
                break;

            case MarkerShape::Point:
                DrawPointMarker(
                    SC,
                    Fill,
                    CenterX,
                    MarkerSize,
                    Color);
                break;
        }
    }
}

void DrawCurrentTradeFillsToChart(
    HWND WindowHandle,
    HDC DeviceContext,
    SCStudyInterfaceRef SC);

SCSFExport scsf_MKLCurrentTradeFills(SCStudyInterfaceRef SC)
{
    SCInputRef MarkerSize = SC.Input[0];
    SCInputRef MarkerStyle = SC.Input[1];
    SCInputRef ShowConnectingLines = SC.Input[2];
    // Append new Inputs to preserve the existing indices 0..2.
    SCInputRef LongEntryColor = SC.Input[3];
    SCInputRef LongExitColor = SC.Input[4];
    SCInputRef ShortEntryColor = SC.Input[5];
    SCInputRef ShortExitColor = SC.Input[6];
    SCInputRef ProfitConnectionColor = SC.Input[7];
    SCInputRef LossConnectionColor = SC.Input[8];
    SCInputRef FillDisplayMode = SC.Input[9];

    if (SC.SetDefaults)
    {
        SC.GraphName = "MKL Current Trade Fills";
        SC.StudyDescription =
            "Displays only the order fills belonging to the currently open "
            "Position for the chart Symbol and selected Trade Account. "
            "Offers triangle, horizontal-line, and point markers. Triangle "
            "entries point right and triangle exits point left, with their "
            "tips aligned to the chart bar. FIFO entry/exit connections are "
            "dotted and colored by profit or loss. All six marker and "
            "connection colors are configurable in Study Inputs. Includes "
            "scale-ins and partial exits. Optional Open Entries Only mode "
            "shows only FIFO-unmatched entry fills. This is a display-only "
            "study and "
            "never submits, modifies, or cancels orders.";

        SC.GraphRegion = 0;
        SC.AutoLoop = 0;
        SC.ValueFormat = VALUEFORMAT_INHERITED;
        SC.ScaleRangeType = SCALE_SAMEASREGION;
        SC.HideDLLAndFunctionNames = 1;
        SC.GlobalDisplayStudySubgraphsNameAndValue = 0;
        SC.DisplayStudyInputValues = 0;
        SC.UpdateAlways = 1;
        SC.MaintainTradeStatisticsAndTradesData = 1;

        MarkerSize.Name = "Marker Size in Pixels";
        MarkerSize.SetInt(6);
        MarkerSize.SetIntLimits(2, 30);

        MarkerStyle.Name = "Marker Style";
        MarkerStyle.SetCustomInputStrings(
            "Triangle;Horizontal Line;Point");
        MarkerStyle.SetCustomInputIndex(0);

        ShowConnectingLines.Name = "Show Entry/Exit Connecting Lines";
        ShowConnectingLines.SetYesNo(1);

        LongEntryColor.Name = "Long Entry Color (Buy Entry)";
        LongEntryColor.SetColor(DEFAULT_LONG_ENTRY_COLOR);

        LongExitColor.Name = "Long Exit Color (Sell Exit)";
        LongExitColor.SetColor(DEFAULT_LONG_EXIT_COLOR);

        ShortEntryColor.Name = "Short Entry Color (Sell Entry)";
        ShortEntryColor.SetColor(DEFAULT_SHORT_ENTRY_COLOR);

        ShortExitColor.Name = "Short Exit Color (Buy Exit)";
        ShortExitColor.SetColor(DEFAULT_SHORT_EXIT_COLOR);

        ProfitConnectionColor.Name = "Connecting Line Color - Profit / Breakeven";
        ProfitConnectionColor.SetColor(DEFAULT_PROFIT_CONNECTION_COLOR);

        LossConnectionColor.Name = "Connecting Line Color - Loss";
        LossConnectionColor.SetColor(DEFAULT_LOSS_CONNECTION_COLOR);

        FillDisplayMode.Name = "Fill Display Mode";
        FillDisplayMode.SetCustomInputStrings("All Fills;Open Entries Only");
        FillDisplayMode.SetCustomInputIndex(0);

        return;
    }

    if (SC.LastCallToFunction)
    {
        SC.p_GDIFunction = nullptr;
        return;
    }

    // Keep the chart's order-fill list available to GetOrderFillEntry().
    SC.MaintainTradeStatisticsAndTradesData = 1;

    // Refresh the callback address after a DLL rebuild/reload.
    SC.p_GDIFunction = DrawCurrentTradeFillsToChart;
}

void DrawCurrentTradeFillsToChart(
    HWND WindowHandle,
    HDC DeviceContext,
    SCStudyInterfaceRef SC)
{
    (void)WindowHandle;
    (void)DeviceContext;

    if (SC.HideStudy || SC.ArraySize <= 0)
        return;

    std::vector<CurrentFill> CurrentFills;
    if (!GetCurrentPositionFills(SC, CurrentFills))
        return;

    if (!AssignPixelCoordinates(SC, CurrentFills))
        return;

    std::vector<FillConnection> Connections;
    BuildFillRolesAndConnections(CurrentFills, Connections);

    int MarkerSize = SC.Input[0].GetInt();
    if (MarkerSize < 2)
        MarkerSize = 2;

    int MarkerStyleIndex = SC.Input[1].GetIndex();
    if (MarkerStyleIndex < 0 || MarkerStyleIndex > 2)
        MarkerStyleIndex = 0;

    const MarkerShape Shape =
        static_cast<MarkerShape>(MarkerStyleIndex);

    const FillColors Colors = {
        SC.Input[3].GetColor(),
        SC.Input[4].GetColor(),
        SC.Input[5].GetColor(),
        SC.Input[6].GetColor(),
        SC.Input[7].GetColor(),
        SC.Input[8].GetColor()
    };

    const bool OpenEntriesOnly = SC.Input[9].GetIndex() == 1;

    if (!OpenEntriesOnly && SC.Input[2].GetYesNo() != 0)
    {
        for (const FillConnection& Connection : Connections)
        {
            if (Connection.EntryFillIndex < 0
                || Connection.ExitFillIndex < 0
                || Connection.EntryFillIndex
                    >= static_cast<int>(CurrentFills.size())
                || Connection.ExitFillIndex
                    >= static_cast<int>(CurrentFills.size()))
            {
                continue;
            }

            const CurrentFill& Entry =
                CurrentFills[Connection.EntryFillIndex];
            const CurrentFill& Exit =
                CurrentFills[Connection.ExitFillIndex];

            const uint32_t ConnectingLineColor =
                Connection.IsProfitable
                ? Colors.ProfitConnection
                : Colors.LossConnection;

            const int EntryConnectionX = GetMarkerConnectionX(
                Entry,
                MarkerSize,
                Shape);
            const int ExitConnectionX = GetMarkerConnectionX(
                Exit,
                MarkerSize,
                Shape);

            DrawLine(
                SC,
                EntryConnectionX,
                Entry.Y,
                ExitConnectionX,
                Exit.Y,
                ConnectingLineColor,
                CONNECTING_LINE_WIDTH,
                LINESTYLE_DOT);
        }
    }

    for (const CurrentFill& Fill : CurrentFills)
    {
        if (OpenEntriesOnly
            && (!IsEntryRole(Fill.Role) || Fill.RemainingOpenQuantity == 0))
        {
            continue;
        }

        if (Fill.BarIndex < SC.IndexOfFirstVisibleBar
            || Fill.BarIndex > SC.IndexOfLastVisibleBar)
        {
            continue;
        }

        DrawFillMarker(
            SC,
            Fill,
            MarkerSize,
            Shape,
            Colors);
    }
}

// Independent, display-only DOM study. Both studies can be selected separately
// from the same custom DLL; no duplicate fill-reconstruction code is needed.
SCSFExport scsf_MKLCurrentTradeFillsDOM(SCStudyInterfaceRef SC)
{
    SCInputRef LongEntryColor = SC.Input[0];
    SCInputRef ShortEntryColor = SC.Input[1];
    // Append new inputs: existing DOM colors retain their saved indices.
    SCInputRef ShowFIFOOrderNumbers = SC.Input[2];

    if (SC.SetDefaults)
    {
        SC.GraphName = "MKL Current Trade Fills DOM";
        SC.StudyDescription =
            "Shows only prices of FIFO-unmatched entry fills from the currently "
            "open position in the right-side price scale. Uses standard "
            "Value Labels and optional FIFO order number Name Labels at the "
            "right edge of the chart, directly left of the price scale. "
            "No lot counts or chart markers. Multiple open "
            "fills at the same entry price share one price label; the FIFO "
            "number reflects the oldest still-open fill at that price. "
            "A maximum of 60 distinct entry prices can be displayed. "
            "Display-only: does "
            "not submit, modify, or cancel orders.";

        SC.GraphRegion = 0;
        SC.AutoLoop = 0;
        SC.ValueFormat = VALUEFORMAT_INHERITED;
        SC.ScaleRangeType = SCALE_SAMEASREGION;
        SC.HideDLLAndFunctionNames = 1;
        SC.GlobalDisplayStudySubgraphsNameAndValue = 0;
        SC.DisplayStudyName = 0;
        SC.DisplayStudyInputValues = 0;
        SC.UpdateAlways = 1;
        SC.MaintainTradeStatisticsAndTradesData = 1;

        LongEntryColor.Name = "Long Entry Color";
        LongEntryColor.SetColor(DEFAULT_LONG_ENTRY_COLOR);
        ShortEntryColor.Name = "Short Entry Color";
        ShortEntryColor.SetColor(DEFAULT_SHORT_ENTRY_COLOR);

        ShowFIFOOrderNumbers.Name = "Show FIFO Order Numbers";
        ShowFIFOOrderNumbers.SetYesNo(0);

        for (int Level = 0; Level < SC_SUBGRAPHS_AVAILABLE; ++Level)
        {
            SCString SubgraphName;
            SubgraphName.Format("Open Entry Price %02d", Level + 1);
            SC.Subgraph[Level].Name = SubgraphName;
            SC.Subgraph[Level].DrawStyle =
                DRAWSTYLE_SUBGRAPH_NAME_AND_VALUE_LABELS_ONLY;
            SC.Subgraph[Level].LineLabel =
                LL_DISPLAY_VALUE | LL_VALUE_ALIGN_VALUES_SCALE;
            SC.Subgraph[Level].DrawZeros = 0;
            SC.Subgraph[Level].PrimaryColor = DEFAULT_LONG_ENTRY_COLOR;
        }

        return;
    }

    if (SC.LastCallToFunction || SC.ArraySize <= 0)
        return;

    SC.MaintainTradeStatisticsAndTradesData = 1;

    const int LastIndex = SC.ArraySize - 1;
    int& PreviousLastIndex = SC.GetPersistentInt(1);
    int& LimitWarningShown = SC.GetPersistentInt(2);

    // Remove the previous last bar's values after a new bar was added. This
    // prevents a prior position's label data from remaining in chart history.
    if (PreviousLastIndex != LastIndex
        && PreviousLastIndex >= 0
        && PreviousLastIndex < SC.ArraySize)
    {
        for (int Level = 0; Level < SC_SUBGRAPHS_AVAILABLE; ++Level)
            SC.Subgraph[Level].Data[PreviousLastIndex] = 0.0f;
    }
    PreviousLastIndex = LastIndex;

    // Keep Value Labels in the price scale. Place optional FIFO Name Labels
    // at the right edge of the chart area, immediately left of that scale.
    // This does not require a separate DOM Label Column.
    const bool ShowNumbers = ShowFIFOOrderNumbers.GetYesNo() != 0;
    const int LabelFlags = LL_DISPLAY_VALUE | LL_VALUE_ALIGN_VALUES_SCALE
        | (ShowNumbers
            ? (LL_DISPLAY_NAME | LL_NAME_ALIGN_FAR_RIGHT | LL_NAME_ALIGN_CENTER)
            : 0);

    // Clear first, even when flat or fill history is temporarily unavailable.
    // The previous labels then disappear naturally on this study update.
    for (int Level = 0; Level < SC_SUBGRAPHS_AVAILABLE; ++Level)
    {
        SC.Subgraph[Level].Data[LastIndex] = 0.0f;
        SC.Subgraph[Level].LineLabel = LabelFlags;
    }

    std::vector<CurrentFill> CurrentFills;
    if (!GetCurrentPositionFills(SC, CurrentFills))
    {
        LimitWarningShown = 0;
        return;
    }

    std::vector<FillConnection> UnusedConnections;
    BuildFillRolesAndConnections(CurrentFills, UnusedConnections);

    // Mark a price level only once, regardless of how many open entry fills
    // occurred at that exact price. Do not round by SC.TickSize because a DOM
    // may combine multiple minimum ticks into one visible price row.
    std::vector<double> OpenEntryPrices;
    for (const CurrentFill& Fill : CurrentFills)
    {
        if (!IsEntryRole(Fill.Role) || Fill.RemainingOpenQuantity == 0)
            continue;

        const double EntryPrice = Fill.FillData.FillPrice;
        if (!std::isfinite(EntryPrice) || EntryPrice <= 0.0)
            continue;

        bool AlreadyShown = false;
        for (const double ExistingPrice : OpenEntryPrices)
        {
            if (std::fabs(EntryPrice - ExistingPrice) <= EPSILON)
            {
                AlreadyShown = true;
                break;
            }
        }

        if (!AlreadyShown)
            OpenEntryPrices.push_back(EntryPrice);
    }

    // Never silently omit individual price levels if the Subgraph limit is hit.
    if (OpenEntryPrices.size() > SC_SUBGRAPHS_AVAILABLE)
    {
        if (!LimitWarningShown)
        {
            SC.AddMessageToLog(
                "MKL Current Trade Fills DOM: More than 60 distinct open "
                "entry prices. No price markers shown (Subgraph limit).", 1);
            LimitWarningShown = 1;
        }
        return;
    }
    LimitWarningShown = 0;

    uint32_t LabelColor = LongEntryColor.GetColor();
    for (const CurrentFill& Fill : CurrentFills)
    {
        if (Fill.RemainingOpenQuantity == 0 || !IsEntryRole(Fill.Role))
            continue;
        LabelColor = Fill.Role == FillRole::LongEntry
            ? LongEntryColor.GetColor()
            : ShortEntryColor.GetColor();
        break;
    }

    // OpenEntryPrices is ordered by the oldest surviving FIFO entry per
    // distinct price. Rebuilding it on each update automatically renumbers
    // the displayed levels when the earliest open fill is fully exhausted.
    for (int Level = 0; Level < static_cast<int>(OpenEntryPrices.size()); ++Level)
    {
        if (ShowNumbers)
        {
            SCString FIFOOrderName;
            FIFOOrderName.Format("%d", Level + 1);
            SC.Subgraph[Level].Name = FIFOOrderName;
            // Sierra Chart uses ShortName (when set) for the Name Label.
            SC.Subgraph[Level].ShortName = FIFOOrderName;
        }

        SC.Subgraph[Level].PrimaryColor = LabelColor;
        SC.Subgraph[Level].Data[LastIndex] =
            static_cast<float>(OpenEntryPrices[Level]);
    }
}
