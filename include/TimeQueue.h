#ifndef TIMEQUEUE_TIMEQUEUE_H
#define TIMEQUEUE_TIMEQUEUE_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>

namespace tq {

using TimerID = uint64_t;
using Handler = std::function<void(bool)>;

template <class CLOCK> class TimeQueue {
private:
  struct QueueElement {
    std::chrono::time_point<CLOCK> tp;
    TimerID tid;
    bool aborted;
    Handler handler;

    bool operator<(const QueueElement &rhs) const { return rhs.tp < tp; }

    QueueElement Cancel() {
      QueueElement result = QueueElement{CLOCK::now(), tid, true, handler};

      tid = 0;
      aborted = true;
      handler = nullptr;
      return result;
    }

    void Handle() {
      if (handler) {
        handler(aborted);
      }
    }
  };

  class Queue : public std::priority_queue<QueueElement> {
  public:
    std::vector<QueueElement> &GetContainer() { return this->c; }
  };

public:
  TimeQueue() {
    workerThread = std::thread([&]() {
      std::unique_lock<std::mutex> l(mutex);
      while (!stopWorkerThread) {
        Wait(l);
        CallHandlers(l);
      }
    });
  }

  ~TimeQueue() {
    Schedule(CLOCK::now(), [&](bool) { stopWorkerThread = true; });
    workerThread.join();
    ClearAll();
  }

  TimerID Schedule(std::chrono::time_point<CLOCK> tp, Handler h) {
    std::lock_guard<std::mutex> l(mutex);
    auto id = AddElement(tp, h);
    cd.notify_one();
    return id;
  }

  TimerID Schedule(std::chrono::milliseconds duration, Handler h) {
    return Schedule(CLOCK::now() + duration, h);
  }

  bool Cancel(TimerID tid) {
    std::lock_guard<std::mutex> l(mutex);
    bool timerCanceled = false;
    QueueElement canceledElement;

    for (auto &element : queue.GetContainer()) {
      if (element.tid == tid) {
        canceledElement = element.Cancel();
        timerCanceled = true;
        break;
      }
    }

    if (timerCanceled) {
      queue.push(canceledElement);
      cd.notify_one();
    }
    return timerCanceled;
  }

private:
  void Wait(std::unique_lock<std::mutex> &l) {
    if (queue.empty()) {
      cd.wait(l, [this]() { return !queue.empty(); });
    } else {
      auto lastSize = queue.size();
      cd.wait_until(l, queue.top().tp, [this, lastSize]() {
        return queue.top().tp <= CLOCK::now() || lastSize != queue.size();
      });
    }
  }

  TimerID AddElement(std::chrono::time_point<CLOCK> tp, Handler h) {
    queue.push(std::move(QueueElement{tp, ++currentTID, false, h}));
    return currentTID;
  }

  void CallHandlers(std::unique_lock<std::mutex> &l) {
    while (!queue.empty() && queue.top().tp < CLOCK::now()) {
      QueueElement e{std::move(queue.top())};
      queue.pop();
      l.unlock();
      e.Handle();
      l.lock();
    }
  }

  void ClearAll() {
    std::lock_guard<std::mutex> l(mutex);
    while (!queue.empty()) {
      queue.pop();
    }
  }

  Queue queue;
  std::mutex mutex;
  std::condition_variable cd;
  TimerID currentTID{1};
  bool stopWorkerThread{false};
  std::thread workerThread;
};

using SteadyTimeQueue = TimeQueue<std::chrono::steady_clock>;
using SystemClockTimeQueue = TimeQueue<std::chrono::system_clock>;
using HighResolutionTimeQueue = TimeQueue<std::chrono::high_resolution_clock>;
} // namespace tq
#endif // TIMEQUEUE_TIMEQUEUE_H
