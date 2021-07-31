#pragma once

#include <numeric>
#include <vector>

#include "geo/tile.h"

#include "utl/erase_if.h"
#include "utl/to_vec.h"
#include "utl/verify.h"

#include "tiles/db/pack_file.h"
#include "tiles/util.h"
#include "tiles/util_parallel.h"

namespace tiles {

struct tile_record {
  geo::tile tile_{};
  std::vector<pack_record> records_;
};

struct tile_record_single {
  tile_record_single() = default;
  tile_record_single(geo::tile tile, pack_record record)
      : tile_{tile}, record_{record} {}

  geo::tile tile_;
  pack_record record_;
};

constexpr size_t const kRepackInFlightMemory = 1024ULL * 1024 * 1024;
constexpr auto kRepackBatchSize = 32;

template <typename Buf, typename PackHandle>
struct repack_memory_manager {
  repack_memory_manager(PackHandle& pack_handle, std::vector<tile_record> tasks)
      : pack_handle_{pack_handle}, tasks_{std::move(tasks)} {
    utl::erase_if(tasks_, [](auto const& t) { return t.records_.empty(); });
    for (auto& task : tasks_) {
      std::sort(
          begin(task.records_), end(task.records_),
          [](auto const& a, auto const& b) { return a.offset_ > b.offset_; });
    }
    std::sort(begin(tasks_), end(tasks_), [](auto const& a, auto const& b) {
      return a.records_.back().offset_ > b.records_.back().offset_;
    });
  }

  tile_record dequeue_task() {
    utl::verify(!tasks_.empty(), "dequeue_task: no more tasks");
    auto task = std::move(tasks_.back());
    tasks_.pop_back();
    return task;
  }

  void insert_result(geo::tile const tile, Buf const& buf) {
    if (tasks_.empty()) {  // no more pending tasks!
      updates_.emplace_back(tile, pack_handle_.append(buf));
      return;
    }

    auto const end_offset = tasks_.back().records_.back().offset_;
    utl::verify(insert_offset_ <= end_offset, "try_insert: invalid offsets");
    if (buf.size() > (end_offset - insert_offset_)) {
      back_stash_.emplace_back(tile, pack_handle_.append(buf));
      return;
    }

    updates_.emplace_back(tile, pack_handle_.insert(insert_offset_, buf));
    insert_offset_ += buf.size();
  }

  void defragment_pack_file() {
    struct owned_pack {
      size_t task_idx_, record_idx_;
      pack_record* record_;
    };

    size_t end_offset = pack_handle_.size();
    std::vector<owned_pack> q_frag, q_defrag;  // double bufing
    {
      size_t used_space = 0;
      size_t largest_record = 0;
      for (auto i = size_t{0U}; i < tasks_.size(); ++i) {
        for (auto j = size_t{0U}; j < tasks_[i].records_.size(); ++j) {
          auto& record = tasks_[i].records_[j];
          used_space += record.size_;
          largest_record = std::max(largest_record, record.size_);
          q_frag.push_back({i, j, &record});
        }
      }
      std::sort(begin(q_frag), end(q_frag), [](auto const& a, auto const& b) {
        return *a.record_ < *b.record_;
      });

      utl::verify(end_offset >= used_space,
                  "defragment: invalid input (more space used than "
                  "available)");
      utl::verify(
          largest_record < end_offset - used_space,
          "defragment: largest_record > working free space ({}, {}, {})",
          largest_record, end_offset, used_space);
#ifndef TILES_REPACK_FEATURES_SILENT
      t_log("defragment : free space {}",
            printable_bytes{end_offset - used_space});
#endif
    }

    auto const total_records = q_frag.size();

    auto task_idx = 0ULL;
    auto record_idx = 0ULL;

    auto q_frag_idx = 0ULL;
    auto refresh_defrag_queue =
        [&, last_task_idx = std::numeric_limits<size_t>::max(),
         last_record_idx = std::numeric_limits<size_t>::max()]() mutable {
          while (!q_frag.empty() || !q_defrag.empty()) {
            if (q_frag.size() <= q_frag_idx || q_frag.empty()) {
              std::swap(q_frag, q_defrag);
              q_defrag.clear();
              q_frag_idx = 0;

#ifndef TILES_REPACK_FEATURES_SILENT
              t_log("fragmented records left {}/{}",  //
                    q_frag.size(), total_records);
#endif

              utl::verify(
                  task_idx != last_task_idx || record_idx != last_record_idx,
                  "defragment: no progress since last queue swap");
              last_task_idx = task_idx;
              last_record_idx = record_idx;
            } else if (auto const& e = q_frag[q_frag_idx];
                       std::tie(e.task_idx_, e.record_idx_) <
                       std::tie(task_idx, record_idx)) {
              ++q_frag_idx;  // task already finished
            } else if (auto const& e = q_frag.back();
                       std::tie(e.task_idx_, e.record_idx_) <
                       std::tie(task_idx, record_idx)) {
              q_frag.pop_back();  // back already finished
            } else if (!q_defrag.empty() &&
                       std::tie(q_defrag.back().task_idx_,
                                q_defrag.back().record_idx_) <
                           std::tie(task_idx, record_idx)) {
              q_defrag.pop_back();  // back already finished
            } else {
              break;
            }
          }
        };

    auto peek_last_fragmented = [&] {
      refresh_defrag_queue();
      utl::verify(!q_frag.empty(), "peek_last_fragmented: q_frag empty");
      return q_frag.back();
    };
    auto peek_next_fragmented = [&] {
      refresh_defrag_queue();
      utl::verify(!q_frag.empty(), "peek_next_fragmented: q_frag empty");
      return q_frag.at(q_frag_idx);
    };
    auto get_last_fragmented = [&] {
      refresh_defrag_queue();
      utl::verify(!q_frag.empty(), "get_last_fragmented: q_frag empty");
      auto e = q_frag.back();
      q_frag.pop_back();
      return e;
    };
    auto get_next_fragmented = [&] {
      refresh_defrag_queue();
      utl::verify(!q_frag.empty(), "get_last_fragmented: q_frag empty");
      return q_frag.at(q_frag_idx++);
    };
    auto defrag_insert_offset = [&] {
      refresh_defrag_queue();
      return q_defrag.empty() ? 0ULL : q_defrag.back().record_->end_offset();
    };
    auto begin_space = [&] {
      refresh_defrag_queue();
      if (q_frag.empty() && q_defrag.empty()) {
        return end_offset;
      } else if (q_defrag.empty()) {
        return peek_next_fragmented().record_->offset_;
      } else if (q_frag.empty()) {
        return end_offset - q_defrag.back().record_->end_offset();
      } else {
        auto const frag_begin = peek_next_fragmented().record_->offset_;
        auto const defrag_end = q_defrag.back().record_->end_offset();
        utl::verify(defrag_end <= frag_begin, "begin_space: invalid {} <= {}",
                    defrag_end, frag_begin);
        return frag_begin - defrag_end;
      }
    };
    auto end_space = [&] {
      refresh_defrag_queue();
      return end_offset -
             (q_frag.empty() ? 0ULL : q_frag.back().record_->end_offset());
    };

    for (; task_idx < tasks_.size(); ++task_idx) {
      for (record_idx = 0; record_idx < tasks_[task_idx].records_.size();
           ++record_idx) {
        auto& record = tasks_[task_idx].records_[record_idx];

        utl::verify(!q_frag.empty(), "q_frag empty!");

        // we are last -> always enough space for move
        auto const& back = peek_last_fragmented();
        if (back.task_idx_ == task_idx && back.record_idx_ == record_idx) {
          end_offset -= record.size_;
          record = pack_handle_.move(end_offset, record);
          utl::verify(record.offset_ != 0, "moved to front (11)");
          continue;
        }

        // make space by moving the blocker somewhere else
        while (end_space() < record.size_) {
          // make space for the blocker move by defragmentation
          while (begin_space() < peek_last_fragmented().record_->size_) {
            auto const insert_offset = defrag_insert_offset();
            auto next = get_next_fragmented();  // mutate after read!
            utl::verify(insert_offset <= next.record_->offset_,
                        "move fragmented in wrong direction");
            *next.record_ = pack_handle_.move(insert_offset, *next.record_);
            q_defrag.push_back(next);
          }

          // move the blocker out of the way
          auto const insert_offset = defrag_insert_offset();
          auto blocker = get_last_fragmented();  // mutate after read!
          utl::verify(insert_offset <= blocker.record_->offset_,
                      "move blocker in wrong direction");
          *blocker.record_ = pack_handle_.move(insert_offset, *blocker.record_);
          q_defrag.push_back(blocker);
        }

        // move the element
        end_offset -= record.size_;
        record = pack_handle_.move(end_offset, record);
        utl::verify(record.offset_ != 0ULL, "moved to front (2)");
      }
    }

    utl::verify(task_idx == tasks_.size(),
                "defragment: not all tasks defragmented.");
  }

  void finish_back_stash() {
    utl::verify(tasks_.empty(), "finish_back_stash: tasks empty");

    for (auto const& [tile, record] : back_stash_) {
      updates_.emplace_back(tile, pack_handle_.move(insert_offset_, record));
      insert_offset_ += record.size_;
    }

    back_stash_.clear();
    pack_handle_.resize(insert_offset_);
  }

  template <typename Cb>
  void housekeeping_flush(Cb const& callback) {
    callback(updates_);
    updates_.clear();
  }

  PackHandle& pack_handle_;

  std::vector<tile_record> tasks_;
  std::vector<tile_record_single> back_stash_;

  size_t insert_offset_{0};
  size_t dequeued_bytes_since_last_defrag_{0};

  std::vector<tile_record_single> updates_;
};

template <typename Buf, typename PackHandle, typename PackFeatures, typename Cb>
void repack_features(PackHandle& pack_handle,
                     std::vector<tile_record> in_tasks,  // NOLINT
                     PackFeatures&& pack_features, Cb&& callback) {
  repack_memory_manager<Buf, PackHandle> mgr{pack_handle, std::move(in_tasks)};
#ifndef TILES_REPACK_FEATURES_SILENT
  progress_tracker pack_progress;
  pack_progress->status("Repack Features")
      .out_mod(5.F)
      .in_high(mgr.tasks_.size());
#endif

  queue_wrapper<std::function<void()>> work_queue;
  queue_wrapper<std::pair<geo::tile, Buf>> result_queue;

  auto const enqueue_work = [&](auto n) {
    auto const enqueue = [&] {
      auto task = mgr.dequeue_task();
      auto packs = utl::to_vec(task.records_, [&](auto const& r) {
        return Buf{pack_handle.get(r)};
      });
      work_queue.enqueue([&, tile = task.tile_, packs = std::move(packs)] {
        result_queue.enqueue({tile, pack_features(tile, packs)});
      });

      return std::accumulate(
          begin(task.records_), end(task.records_), 0ULL,
          [](auto acc, auto const& r) { return acc + r.size_; });
    };

    if (n == -1) {
      size_t enqueued_size = 0;
      while (!mgr.tasks_.empty() && enqueued_size < kRepackInFlightMemory) {
        enqueued_size += enqueue();
      }
    } else {
      while (!mgr.tasks_.empty() && n > 0) {
        enqueue();
        --n;
      }
    }
  };

  auto dequeue_results = [&](auto n) {
    auto const dequeue = [&] {
      std::pair<geo::tile, Buf> result;
      if (result_queue.dequeue(result)) {
        mgr.insert_result(result.first, result.second);
        result_queue.finish();
#ifndef TILES_REPACK_FEATURES_SILENT
        pack_progress->increment();
#endif
      }
    };

    if (n == -1) {  // while there is any work left
      while (!(result_queue.finished() && work_queue.finished())) {
        dequeue();
      }
    } else {  // dequeue some (and enqueue some)
      while (n > 0 && !(result_queue.finished() && work_queue.finished())) {
        dequeue();
        --n;
      }
    }
  };

  {
    queue_processor proc{work_queue};
    enqueue_work(-1);  // some more as buf
    mgr.defragment_pack_file();  // now we have some space
    while (!mgr.tasks_.empty()) {
      dequeue_results(kRepackBatchSize);
      mgr.housekeeping_flush(callback);
      enqueue_work(kRepackBatchSize);
    }
    mgr.finish_back_stash();
    mgr.housekeeping_flush(callback);

    dequeue_results(-1);  // ensure fully drained
    mgr.housekeeping_flush(callback);
  }

#ifndef TILES_REPACK_FEATURES_SILENT
  t_log("pack file utilization: {:.2f}% ({} / {})",
        100. * pack_handle.size() / pack_handle.capacity(),  //
        pack_handle.size(), pack_handle.capacity());
#endif

  utl::verify(mgr.tasks_.empty(), "pack_features: task queue not empty");
  utl::verify(work_queue.queue_.size_approx() == 0,
              "pack_features: work queue not empty");
  utl::verify(result_queue.queue_.size_approx() == 0,
              "pack_features: result queue not empty");
}

}  // namespace tiles
