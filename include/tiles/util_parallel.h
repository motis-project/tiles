#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "blockingconcurrentqueue.h"

namespace tiles {

template <typename T>
struct sequential_until_finish {
  explicit sequential_until_finish(std::function<T()> fn)
      : fn_{std::move(fn)} {}

  std::optional<std::pair<size_t, T>> process() {
    std::lock_guard<std::mutex> l{mutex_};
    if (finished_) {
      return std::nullopt;
    }

    auto t = fn_();
    if (!t) {
      finished_ = true;
      return std::nullopt;
    }
    return std::make_pair(counter_++, std::move(t));
  }

  std::function<T()> fn_;

  std::mutex mutex_;
  bool finished_{false};
  std::size_t counter_{0};
};

template <typename T>
struct in_order_queue {
  template <typename Fn>
  void process_in_order(size_t idx, T t, Fn&& fn) {
    {
      std::lock_guard<std::mutex> l{mutex_};
      if (next_expected_ != idx || active_) {
        dirty_ = true;
        queue_.emplace_back(idx, std::move(t));
        return;
      }
      active_ = true;
    }
    fn(std::move(t));
    {
      std::lock_guard<std::mutex> l{mutex_};
      next_expected_ = idx + 1;
    }

    while (true) {
      std::vector<std::pair<size_t, T>> ready;
      {  // find consecutive next expected
        std::lock_guard<std::mutex> l{mutex_};
        if (dirty_) {
          std::sort(
              begin(queue_), end(queue_),
              [](auto const& a, auto const& b) { return a.first > b.first; });
          dirty_ = false;
        }

        auto next_expected_ready = next_expected_;
        while (!queue_.empty()) {
          if (queue_.back().first != next_expected_ready) {
            break;
          }

          ready.emplace_back(std::move(queue_.back()));
          queue_.pop_back();
          ++next_expected_ready;
        }

        if (ready.empty()) {
          active_ = false;
          return;  // common case after first iteration of outer loop
        }
      }

      // process ready items
      for (auto& x : ready) {
        fn(std::move(x.second));
      }

      {  // increment next-expected
        std::lock_guard<std::mutex> l{mutex_};
        next_expected_ = ready.back().first + 1;
      }
    }
    utl::verify(false, "cannot happen");
  }

  std::mutex mutex_;
  bool dirty_{false};
  bool active_{false};
  size_t next_expected_{0};
  std::vector<std::pair<size_t, T>> queue_;
};

template <typename T>
struct queue_wrapper {
  queue_wrapper() : pending_{0} {}

  void enqueue(T&& t) {
    ++pending_;
    queue_.enqueue(std::forward<T>(t));
  }

  void enqueue_bulk(std::vector<T> const& vec) {
    pending_ += vec.size();
    queue_.enqueue_bulk(vec.data(), vec.size());
  }

  bool dequeue(T& t) {
    return queue_.wait_dequeue_timed(t, std::chrono::milliseconds(10));
  }

  void dequeue_bulk(std::vector<T>& vec) {
    size_t count = queue_.wait_dequeue_bulk_timed(
        vec.data(), vec.size(), std::chrono::milliseconds(10));
    vec.resize(count);
  }

  void add_keep_alive() { ++pending_; }
  void remove_keep_alive() { --pending_; }

  void finish() { --pending_; }
  void finish_bulk(size_t count) { pending_ -= count; }
  bool finished() const { return pending_ == 0; }

  std::atomic_uint64_t pending_;
  moodycamel::BlockingConcurrentQueue<T> queue_;
};

struct queue_processor {
  explicit queue_processor(queue_wrapper<std::function<void()>>& queue)
      : queue_{queue}, shutdown_{false} {
    for (auto i = 0ULL; i < std::thread::hardware_concurrency(); ++i) {
      threads_.emplace_back([&, this] {
        while (true) {
          if (shutdown_) {
            break;
          }

          std::function<void()> fn;
          if (queue_.dequeue(fn)) {
            try {
              fn();
              queue_.finish();
            } catch (...) {
              std::cerr << " exception in queue_processor"
                        << std::this_thread::get_id() << std::endl;
              throw;  // this better not happens ;)
            }
          }
        }
      });
    }
  }

  ~queue_processor() {
    shutdown_ = true;
    std::for_each(begin(threads_), end(threads_), [](auto& t) { t.join(); });
  }

  queue_processor(queue_processor const&) = delete;
  queue_processor(queue_processor&&) = delete;
  queue_processor& operator=(queue_processor const&) = delete;
  queue_processor& operator=(queue_processor&&) = delete;

  queue_wrapper<std::function<void()>>& queue_;
  std::atomic_bool shutdown_;
  std::vector<std::thread> threads_;
};

// template <typename Task, uint64_t MaxInFlight = 64>
// struct throttling_source {
//   static_assert(MaxInFlight > 0);

//   throttling_source(queue_wrapper<std::function<Task>>& queue)
//       : queue_{queue} {}

//   template <typename Fn>
//   auto submit(Fn&& fn) -> std::future<decltype Fn()> {
//     using result_t = typename decltype Fn();

//     std::packaged_task<result_t()> task{
//         [fn = std::forward(fn)] { return fn(); }};

//     std::future<result_type> future_result{task.get_future()};
//     enqueue_blocking([task = std::move(task)](auto...) { task(); });
//     return future_result;
//   }

//   template <typename R, typename... Args>
//   auto submit_env(std::function<R(Args...)>&& fn) ->
//   std::future<R(Args...)>
//   {
//     std::packaged_task<R(Args...)> task{
//         [fn = std::forward(fn)](Args...&&) { return fn(Args...); }};

//     std::future<result_type> future_result{task.get_future()};
//     enqueue_blocking(std::move(task));
//     return future_result;
//   }

//   void enqueue_blocking(Task&& task) {
//     while (true) {
//       std::unique_lock<std::mutex> lk{cv_mutex_};
//       if (in_flight_ < MaxInFlight) {
//         queue.enqueue([task = std::move(task)](auto... args) {
//           task(..args);

//           std::lock_guard<std::mutex> l{cv_mutex_};
//           --in_flight_;
//           cv_.notify_one();
//         });
//         ++in_flight_;
//         break;
//       }

//       cv_.wait(lk, [&] { return in_flight_ < MaxInFlight; });
//     }
//   }

//   std::mutex cv_mutex_;
//   std::condition_variable cv_;
//   uint64_t in_flight_ = 0;

//   // std::function<void()>
//   queue_wrapper<Task>& queue_;
// };

}  // namespace tiles
