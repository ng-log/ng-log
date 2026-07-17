# Migrating from glog

To migrate from glog to ng-log, you'll need to adjust your code as follows:

-   replace `<glog/logging.h>` with `<ng-log/logging.h>` and similarly for other heads (flags, log_severity, stl_logging, etc)
-   replace the `google::` namespace with `nglog::`
-   replace `InitGoogleLogging` with `InitializeLogging`
-   replace `IsGoogleLoggingInitialized` with `IsLoggingInitialized`
-   replace `ShutdownGoogleLogging` with `ShutdownLogging`
