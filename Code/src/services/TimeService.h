#pragma once

namespace TimeService {

// Non-blocking SNTP sync (pool.ntp.org / time.nist.gov). The clock is used
// for logs and uptime only - nothing depends on it being synced.
void begin();

}  // namespace TimeService
