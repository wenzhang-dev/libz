#pragma once

// Copyright 2016 Juho Snellman, released under a MIT license (see
// LICENSE).
//
// SPDX-License-Identifier: MIT
//
// A timer queue which allows events to be scheduled for execution
// at some later point. Reasons you might want to use this implementation
// instead of some other are:
//
// - A single-file C++11 implementation with no external dependencies.
// - Optimized for high occupancy rates, on the assumption that the
//   utilization of the timer queue is proportional to the utilization
//   of the system as a whole. When a tradeoff needs to be made
//   between efficiency of one operation at a low occupancy rate and
//   another operation at a high rate, we choose the latter.
// - Tries to minimize the cost of event rescheduling or cancelation,
//   on the assumption that a large percentage of events will never
//   be triggered. The implementation avoids unnecessary work when an
//   event is rescheduled, and provides a way for the user specify a
//   range of acceptable execution times instead of just an exact one.
// - Facility for limiting the number of events to execute on a
//   single invocation, to allow fine grained interleaving of timer
//   processing and application logic.
// - An interface that at least the author finds convenient.
//
// The exact implementation strategy is a hierarchical timer
// wheel. A timer wheel is effectively a ring buffer of linked lists
// of events, and a pointer to the ring buffer. As the time advances,
// the pointer moves forward, and any events in the ring buffer slots
// that the pointer passed will get executed.
//
// A hierarchical timer wheel layers multiple timer wheels running at
// different resolutions on top of each other. When an event is
// scheduled so far in the future than it does not fit the innermost
// (core) wheel, it instead gets scheduled on one of the outer
// wheels. On each rotation of the inner wheel, one slot's worth of
// events are promoted from the second wheel to the core. On each
// rotation of the second wheel, one slot's worth of events is
// promoted from the third wheel to the second, and so on.

#include <limits>
#include <memory>

#include "error.h"
#include "macros.h"

namespace libz {

using Tick = std::uint64_t;

class TimerWheel;
namespace _ {
class TimerWheelSlot;
}

class TimerEventBase {
 public:
  virtual ~TimerEventBase() { Cancel(); }
  inline void Cancel();
  bool IsActive() const { return slot_ != nullptr; }

  Tick scheduled_at() const { return scheduled_at_; }

  TimerEventBase(TimerEventBase&&) = default;
  TimerEventBase& operator=(TimerEventBase&&) = default;

 protected:
  TimerEventBase() = default;
  virtual void Execute() = 0;

  virtual void OnAbort() {}
  virtual void OnCancel(Error&&) {}

 private:
  void SetScheduleAt(Tick ts) { scheduled_at_ = ts; }
  inline void Relink(_::TimerWheelSlot* slot);

 private:
  Tick scheduled_at_;
  _::TimerWheelSlot* slot_{nullptr};

  TimerEventBase* next_{nullptr};
  TimerEventBase* prev_{nullptr};

  friend _::TimerWheelSlot;
  friend TimerWheel;
  DISALLOW_COPY_AND_ASSIGN(TimerEventBase);
};

template <typename H>
class TimerEvent : public TimerEventBase {
 public:
  TimerEvent(H&& h) : h_(std::move(h)) {}

 private:
  void Execute() override { h_(); }

  H h_;
};

namespace _ {

class TimerWheelSlot {
 public:
  TimerWheelSlot() = default;

  TimerWheelSlot(TimerWheelSlot&&) = default;
  TimerWheelSlot& operator=(TimerWheelSlot&&) = default;

  void Abort();
  void Cancel(Error&&);

 private:
  const TimerEventBase* events() const { return events_; }
  inline TimerEventBase* PopEvent();
  bool IsEmpty() const { return events_ == nullptr; }

 private:
  TimerEventBase* events_{nullptr};

  friend TimerWheel;
  friend TimerEventBase;
  DISALLOW_COPY_AND_ASSIGN(TimerWheelSlot);
};

TimerEventBase* TimerWheelSlot::PopEvent() {
  auto event = events_;
  events_ = events_->next_;
  if (events_) {
    events_->prev_ = nullptr;
  }
  event->next_ = nullptr;
  event->slot_ = nullptr;
  return event;
}

}  // namespace _

class TimerWheel {
 public:
  TimerWheel(Tick now = 0);

  TimerWheel(TimerWheel&&) = default;
  TimerWheel& operator=(TimerWheel&&) = default;

  // Advance the TimerWheel by the specified number of ticks, and execute
  // any events scheduled for execution at or before that time. The
  // number of events executed can be restricted using the max_execute
  // parameter. If that limit is reached, the function will return false,
  // and the excess events will be processed on a subsequent call.
  //
  // - It is safe to cancel or schedule events from within event callbacks.
  // - During the execution of the callback the observable event tick will
  //   be the tick it was scheduled to run on; not the tick the clock will
  //   be advanced to.
  // - Events will happen in order; all events scheduled for tick X will
  //   be executed before any event scheduled for tick X+1.
  //
  // Delta should be non-0. The only exception is if the previous
  // call to advance() returned false.
  //
  // advance() should not be called from an event callback.
  bool Advance(
      Tick delta,
      std::size_t max_execute = std::numeric_limits<std::size_t>::max(),
      int level = 0);

  // Schedule the event to be executed delta ticks from the current time.
  // The delta must be non-0.
  void Schedule(TimerEventBase* event, Tick delta);

  // Schedule the event to happen at some time between start and end
  // ticks from the current time. The actual time will be determined
  // by the TimerWheel to minimize rescheduling and promotion overhead.
  // Both start and end must be non-0, and the end must be greater than
  // the start.
  void ScheduleInRange(TimerEventBase* event, Tick start, Tick end);

  Tick now() const { return now_[0]; }

  Tick TicksToNextEvent(Tick max = std::numeric_limits<Tick>::max(),
                        int level = 0);

  bool IsEmpty() const;

  void Cancel(Error&&);
  void Abort();

 private:
  inline bool ProcessCurrentSlot(Tick now, std::size_t max_execute, int level);

 private:
  static constexpr int kWidthBits = 8;
  static constexpr int kNumLevels = (64 + kWidthBits - 1) / kWidthBits;
  static constexpr int kMaxLevel = kNumLevels - 1;
  static constexpr int kNumSlots = 1 << kWidthBits;
  static constexpr int kMask = kNumSlots - 1;

  Tick now_[kNumLevels];
  Tick ticks_pending_;
  _::TimerWheelSlot slots_[kNumLevels][kNumSlots];

  DISALLOW_COPY_AND_ASSIGN(TimerWheel);
};

inline void TimerEventBase::Cancel() {
  if (!slot_) return;
  Relink(nullptr);
}

inline void TimerEventBase::Relink(_::TimerWheelSlot* new_slot) {
  if (new_slot == slot_) return;

  // unlink from old location
  if (slot_) {
    auto prev = prev_;
    auto next = next_;
    if (next) {
      next->prev_ = prev;
    }
    if (prev) {
      prev->next_ = next;
    } else {
      slot_->events_ = next;
    }
  }

  // insert in new slot
  {
    if (new_slot) {
      auto old = new_slot->events_;
      next_ = old;
      if (old) {
        old->prev_ = this;
      }
      new_slot->events_ = this;
    } else {
      next_ = nullptr;
    }
    prev_ = nullptr;
  }
  slot_ = new_slot;
}

}  // namespace libz

