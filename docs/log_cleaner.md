<!--
SPDX-FileCopyrightText: 2024 Google Inc.
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# Automatically Remove Old Logs

To enable the log cleaner:

``` cpp
using namespace std::chrono_literals;
nglog::EnableLogCleaner(24h * 3); // keep your logs for 3 days
```

C++20 provides a duration in days:

``` cpp
nglog::EnableLogCleaner(std::chrono::days{3}); // keep your logs for 3 days
```

A background thread then checks for overdue logs right away and every
`--logcleansecs` seconds afterward, 300 by default, as well as whenever a new
log file is created. In this example, any log file from your project whose last
modified time is greater than 3 days will be `unlink`()ed.

The cleaner identifies log files using the filename configuration active in the
current process. If a filename-related option changes between processes, files
created by an earlier process may not be recognized and may remain after
cleanup. Keep filename-related options consistent when cleanup should cover
files created by earlier processes.

This feature can be disabled at any time using
``` cpp
nglog::DisableLogCleaner();
```
If the cleaner was enabled, this performs a final check before it returns.
Otherwise, it deletes nothing.
