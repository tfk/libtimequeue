#include <iostream>
#include "TimeQueue.h"

uint64_t timestamp() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int main() {
  tq::SteadyTimeQueue tq;

  for (int i = 0, delta = 5000; i < 5; i++, delta -= 500) {
    auto ts = timestamp();
    auto t = tq.Schedule(std::chrono::milliseconds(delta), [ts](bool) {
      auto tsrun = timestamp();
      std::cout << "[" << tsrun << "] Callback called after: " << tsrun - ts << std::endl;
    });
    std::cout << "[" << ts << "] Scheduling in " << delta << " ms - id: " << t << std::endl;
  }
  std::this_thread::sleep_for(std::chrono::seconds(4));
  return 0;
}
