// Stand-in for the time-server calls net.cpp makes. See netsim.h.
#pragma once

enum sntp_sync_status_t { SNTP_SYNC_STATUS_RESET = 0, SNTP_SYNC_STATUS_COMPLETED = 1, SNTP_SYNC_STATUS_IN_PROGRESS = 2 };
sntp_sync_status_t sntp_get_sync_status();
void sntp_set_sync_status(sntp_sync_status_t s);
bool sntp_enabled();
void sntp_stop();
void configTzTime(const char *tz, const char *server1, const char *server2 = nullptr, const char *server3 = nullptr);
