# Strip Logging Messages

Strings used in log messages can increase the size of your binary and present a
privacy concern. You can therefore instruct ng-log to remove all strings which
fall below a certain severity level by using the `NGLOG_STRIP_LOG` macro:

If your application has code like this:

``` cpp
#define NGLOG_STRIP_LOG 1    // this must go before the #include!
#include <ng-log/logging.h>
```

The compiler will remove the log messages whose severities are less than
the specified integer value. Since `VLOG` logs at the severity level
`INFO` (numeric value `0`), setting `NGLOG_STRIP_LOG` to 1 or greater
removes all log messages associated with `VLOG`s as well as `INFO` log
statements.
