#pragma once

namespace BossRoomAssistDisplay {

void SetMultipliers(unsigned char physical, unsigned char magic, unsigned char physicalPanel);
void ResetMultipliers();
bool Hook(bool enable);
bool IsReady();

} // namespace BossRoomAssistDisplay
