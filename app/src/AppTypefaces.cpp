// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AppTypefaces.h"

#include <az_ui/az_ui.h>

#include <BinaryData.h>

void installAppTypefaces()
{
    az::ui::TypefaceSet faces;
    faces.legendSemiBold = { BinaryData::SairaCondensedSemiBold_ttf,
                             BinaryData::SairaCondensedSemiBold_ttfSize };
    faces.legendBold     = { BinaryData::SairaCondensedBold_ttf,
                             BinaryData::SairaCondensedBold_ttfSize };
    faces.body           = { BinaryData::IBMPlexSansRegular_ttf,
                             BinaryData::IBMPlexSansRegular_ttfSize };
    faces.mono           = { BinaryData::IBMPlexMonoRegular_ttf,
                             BinaryData::IBMPlexMonoRegular_ttfSize };
    faces.monoMedium     = { BinaryData::IBMPlexMonoMedium_ttf,
                             BinaryData::IBMPlexMonoMedium_ttfSize };
    az::ui::setTypefaces (faces);
}
