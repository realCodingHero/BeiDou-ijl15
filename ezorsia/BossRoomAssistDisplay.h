#pragma once

namespace BossRoomAssistDisplay {

void SetMultipliers(unsigned char physical, unsigned char magic, unsigned char panel);
void ResetMultipliers();
bool Hook(bool enable);
bool IsReady();

} // namespace BossRoomAssistDisplay
