#include "sierrachart.h"

SCDLLName("MKL Visibility Controller")

SCSFExport scsf_MKLVisibilityController(SCStudyInterfaceRef sc)
{
    SCInputRef Input_TargetStudy         = sc.Input[0];
    SCInputRef Input_StartTime           = sc.Input[1];
    SCInputRef Input_EndTime             = sc.Input[2];
    SCInputRef Input_VisibleDuringWindow = sc.Input[3];

    if (sc.SetDefaults)
    {
        sc.GraphName = "MKL Visibility Controller";

        sc.StudyDescription =
            "Controls the visibility of a selected study based on a "
            "configurable time window. The target study can either be "
            "visible during the configured window and hidden outside it, "
            "or hidden during the configured window and visible outside it.";

        sc.AutoLoop = 0;
        sc.GraphRegion = 0;

        // Continuously evaluate the current time, even if no new
        // market data is received.
        sc.UpdateAlways = 1;

        // ---------------------------------------------------------
        // Inputs
        // ---------------------------------------------------------

        Input_TargetStudy.Name = "Target Study";
        Input_TargetStudy.SetStudyID(1);

        Input_StartTime.Name = "Start Time";
        Input_StartTime.SetTime(HMS_TIME(9, 30, 0));

        Input_EndTime.Name = "End Time";
        Input_EndTime.SetTime(HMS_TIME(15, 59, 59));

        Input_VisibleDuringWindow.Name =
            "Target Study Visible During Time Window";
        Input_VisibleDuringWindow.SetYesNo(1);

        return;
    }

    const int TargetStudyID = Input_TargetStudy.GetStudyID();

    // Do nothing if no valid study has been selected or if the
    // controller itself was accidentally selected.
    if (TargetStudyID <= 0
        || TargetStudyID == sc.StudyGraphInstanceID)
    {
        return;
    }

    SCDateTime CurrentDateTime;

    // During chart replay, use the latest replay/chart time.
    // During normal operation, use the current time in the
    // chart's configured time zone.
    if (sc.IsReplayRunning())
    {
        CurrentDateTime = sc.LatestDateTimeForLastBar;
    }
    else
    {
        CurrentDateTime = sc.GetCurrentDateTime();
    }

    const int CurrentTime = CurrentDateTime.GetTime();
    const int StartTime   = Input_StartTime.GetTime();
    const int EndTime     = Input_EndTime.GetTime();

    bool IsInsideTimeWindow = false;

    // Normal intraday time window:
    // Example: 09:30:00 - 15:59:59
    if (StartTime <= EndTime)
    {
        IsInsideTimeWindow =
            CurrentTime >= StartTime
            && CurrentTime <= EndTime;
    }

    // Also support time windows crossing midnight.
    // Example: 18:00:00 - 09:59:59
    else
    {
        IsInsideTimeWindow =
            CurrentTime >= StartTime
            || CurrentTime <= EndTime;
    }

    const bool VisibleDuringWindow =
        Input_VisibleDuringWindow.GetYesNo() != 0;

    const bool ShouldBeVisible = VisibleDuringWindow
        ? IsInsideTimeWindow
        : !IsInsideTimeWindow;

    const int DesiredVisibility = ShouldBeVisible ? 1 : 0;

    // Only change the visibility state when necessary.
    if (sc.GetStudyVisibilityState(TargetStudyID) != DesiredVisibility)
    {
        sc.SetStudyVisibilityState(
            TargetStudyID,
            DesiredVisibility
        );
    }
}
