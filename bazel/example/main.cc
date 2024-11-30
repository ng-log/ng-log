#include <gflags/gflags.h>
#include <ng-log/logging.h>
#include <ng-log/stl_logging.h>

int main(int argc, char* argv[]) {
  // Initialize logging library.
  nglog::InitializeLogging(argv[0]);

  // Optional: parse command line flags
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  LOG(INFO) << "Hello, world!";

  // nglog/stl_logging.h allows logging STL containers.
  std::vector<int> x;
  x.push_back(1);
  x.push_back(2);
  x.push_back(3);
  LOG(INFO) << "ABC, it's easy as " << x;

  return 0;
}
