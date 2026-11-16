#include "sierrachart.h"

SCDLLName("MKL Visibility Toggle")

SCSFExport scsf_MKLVisibilityToggle(SCStudyInterfaceRef sc)
{
    SCInputRef Input_ButtonNumber = sc.Input[0];

    constexpr int FIRST_TARGET_INPUT = 1;
    constexpr int TARGET_COUNT = 10;

    if (sc.SetDefaults)
    {
        sc.GraphName = "MKL Visibility Toggle";
        sc.StudyDescription =
            "Toggles the visibility of up to 10 studies on the current chart "
            "using an Advanced Custom Study Control Bar button.";

        sc.AutoLoop = 0;
        sc.GraphRegion = 0;

        Input_ButtonNumber.Name = "Control Bar Button Number";
        Input_ButtonNumber.SetInt(1);
        Input_ButtonNumber.SetIntLimits(1, 150);

        for (int i = 0; i < TARGET_COUNT; ++i)
        {
            sc.Input[FIRST_TARGET_INPUT + i].Name.Format("Target Study %d", i + 1);

            // Study ID 0 is the Main Price Graph. In this study it is treated
            // as an unused target so that all ten target inputs can be optional.
            sc.Input[FIRST_TARGET_INPUT + i].SetStudyID(0);
        }

        return;
    }

    const int ButtonNumber = Input_ButtonNumber.GetInt();
    const int ButtonEventID = ACS_BUTTON_1 + (ButtonNumber - 1);

    // Remember the last configured button number so a changed input does not
    // leave the old button visually pressed in.
    int& LastButtonNumber = sc.GetPersistentInt(1);

    if (LastButtonNumber >= 1 && LastButtonNumber <= 150 && LastButtonNumber != ButtonNumber)
        sc.SetCustomStudyControlBarButtonEnable(LastButtonNumber, 0);

    LastButtonNumber = ButtonNumber;

    // Button caption and tooltip are configured centrally in Sierra Chart's
    // Control Bar Button Properties; this study only controls button state.
    if (sc.IsFullRecalculation)
    {
        // Synchronize the visual button state with the target studies without
        // changing any study visibility during initialization/recalculation.
        // ON means every configured target is visible. If the targets are in a
        // mixed state, the button is shown as OFF.
        bool HasValidTarget = false;
        bool AllTargetsVisible = true;

        for (int i = 0; i < TARGET_COUNT; ++i)
        {
            const unsigned int StudyID = sc.Input[FIRST_TARGET_INPUT + i].GetStudyID();

            // ID 0 is intentionally treated as "unused". Also prevent this
            // controller from targeting itself.
            if (StudyID == 0 || StudyID == static_cast<unsigned int>(sc.StudyGraphInstanceID))
                continue;

            HasValidTarget = true;

            if (sc.GetStudyVisibilityState(static_cast<int>(StudyID)) == 0)
                AllTargetsVisible = false;
        }

        sc.SetCustomStudyControlBarButtonEnable(
            ButtonNumber,
            HasValidTarget && AllTargetsVisible ? 1 : 0);
    }

    // Only respond to the Advanced Custom Study Control Bar button assigned to
    // this particular instance of MKL Visibility Toggle.
    if (sc.MenuEventID == ButtonEventID
        && (sc.PointerEventType == SC_ACS_BUTTON_ON
            || sc.PointerEventType == SC_ACS_BUTTON_OFF))
    {
        const int MakeVisible =
            sc.PointerEventType == SC_ACS_BUTTON_ON ? 1 : 0;

        unsigned int ProcessedStudyIDs[TARGET_COUNT] = {};
        int ProcessedCount = 0;

        for (int i = 0; i < TARGET_COUNT; ++i)
        {
            const unsigned int StudyID = sc.Input[FIRST_TARGET_INPUT + i].GetStudyID();

            // ID 0 = unused. Do not allow this controller to hide itself.
            if (StudyID == 0 || StudyID == static_cast<unsigned int>(sc.StudyGraphInstanceID))
                continue;

            // Ignore duplicate target selections.
            bool AlreadyProcessed = false;
            for (int j = 0; j < ProcessedCount; ++j)
            {
                if (ProcessedStudyIDs[j] == StudyID)
                {
                    AlreadyProcessed = true;
                    break;
                }
            }

            if (AlreadyProcessed)
                continue;

            ProcessedStudyIDs[ProcessedCount++] = StudyID;
            sc.SetStudyVisibilityState(static_cast<int>(StudyID), MakeVisible);
        }

        // SetStudyVisibilityState takes effect visually on a chart redraw.
        // Requesting a recalculation guarantees an update even when the market
        // is idle and no normal chart update would otherwise occur.
        sc.RecalculateChart(sc.ChartNumber);
    }

    // Avoid leaving the button in a stale ON state when the study is removed.
    if (sc.LastCallToFunction)
        sc.SetCustomStudyControlBarButtonEnable(ButtonNumber, 0);
}
