#pragma once

// Timestamped line on stdout (the startup hook redirects it to /tmp/retrogc-mister.log).
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
