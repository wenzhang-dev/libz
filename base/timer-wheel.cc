#include "timer-wheel.h"

namespace libz {
namespace _ {

void TimerWheelSlot::Abort() {
  while (!IsEmpty()) {
    if (auto e = PopEvent(); e) {
      e->OnAbort();
    }
  }
}

void TimerWheelSlot::Cancel(Error&& err) {
  while (!IsEmpty()) {
    if (auto e = PopEvent(); e) {
      e->OnCancel(Error{err});
    }
  }
}

}  // namespace _

TimerWheel::TimerWheel(Tick now) : ticks_pending_(0) {
  for (int i = 0; i < kNumLevels; ++i) {
    now_[i] = now >> (kWidthBits * i);
  }
}

void TimerWheel::Abort() {
  for (std::size_t i = 0; i < kNumLevels; ++i) {
    for (std::size_t j = 0; j < kNumSlots; ++j) {
      slots_[i][j].Abort();
    }
  }
}

void TimerWheel::Cancel(Error&& e) {
  for (std::size_t i = 0; i < kNumLevels; ++i) {
    for (std::size_t j = 0; j < kNumSlots; ++j) {
      slots_[i][j].Cancel(Error{e});
    }
  }
}

bool TimerWheel::IsEmpty() const {
  for (std::size_t i = 0; i < kNumLevels; ++i) {
    for (std::size_t j = 0; j < kNumSlots; ++j) {
      if (!slots_[i][j].IsEmpty()) {
        return false;
      }
    }
  }
  return true;
}

bool TimerWheel::Advance(Tick delta, std::size_t max_execute, int level) {
  if (ticks_pending_) {
    if (level == 0) {
      ticks_pending_ += delta;
    }

    Tick now = now_[level];
    if (!ProcessCurrentSlot(now, max_execute, level)) {
      return false;
    }

    if (level == 0) {
      delta = ticks_pending_ - 1;
      ticks_pending_ = 0;
    } else {
      return true;
    }
  } else {
    DCHECK(delta > 0);
  }

  while (delta--) {
    Tick now = ++now_[level];
    if (!ProcessCurrentSlot(now, max_execute, level)) {
      ticks_pending_ = delta + 1;
      return false;
    }
  }

  return true;
}

bool TimerWheel::ProcessCurrentSlot(Tick now, std::size_t max_execute,
                                    int level) {
  std::size_t slot_index = now & kMask;
  auto slot = &slots_[level][slot_index];
  if (slot_index == 0 && level < kMaxLevel) {
    if (!Advance(1, max_execute, level + 1)) {
      return false;
    }
  }

  while (slot->events()) {
    auto event = slot->PopEvent();
    if (level > 0) {
      DCHECK((now_[0] & kMask) == 0);
      if (now_[0] >= event->scheduled_at()) {
        event->Execute();
        if (!--max_execute) {
          return false;
        }
      } else {
        Schedule(event, event->scheduled_at() - now_[0]);
      }
    } else {
      event->Execute();
      if (!--max_execute) {
        return false;
      }
    }
  }

  return true;
}

void TimerWheel::Schedule(TimerEventBase* event, Tick delta) {
  DCHECK(delta > 0);
  event->SetScheduleAt(now_[0] + delta);

  int level = 0;
  for (; delta >= kNumSlots; ++level) {
    delta = (delta + (now_[level] & kMask)) >> kWidthBits;
  }

  std::size_t slot_index = (now_[level] + delta) & kMask;
  auto slot = &slots_[level][slot_index];
  event->Relink(slot);
}

void TimerWheel::ScheduleInRange(TimerEventBase* event, Tick start, Tick end) {
  DCHECK(end > start);
  if (event->IsActive()) {
    auto current = event->scheduled_at() - now_[0];
    if (current >= start && current <= end) {
      return;
    }
  }

  Tick mask = ~0;
  while ((start & mask) != (end & mask)) {
    mask = (mask << kWidthBits);
  }

  Tick delta = end & (mask >> kWidthBits);
  Schedule(event, delta);
}

Tick TimerWheel::TicksToNextEvent(Tick max, int level) {
  if (ticks_pending_) {
    return 0;
  }

  Tick now = now_[0];

  Tick min = max;
  for (int i = 0; i < kNumSlots; ++i) {
    auto slot_index = (now_[level] + i + 1) & kMask;
    if (slot_index == 0 && level < kMaxLevel) {
      if (level > 0 || !slots_[level][slot_index].events()) {
        auto up_slot_index = (now_[level + 1] + 1) & kMask;
        const auto& slot = slots_[level + 1][up_slot_index];
        for (auto event = slot.events(); event != nullptr;
             event = event->next_) {
          min = std::min(min, event->scheduled_at() - now);
        }
      }
    }

    bool found = false;
    const auto& slot = slots_[level][slot_index];
    for (auto event = slot.events(); event != nullptr; event = event->next_) {
      min = std::min(min, event->scheduled_at() - now);
      if (level == 0) {
        return min;
      } else {
        found = true;
      }
    }

    if (found) {
      return min;
    }
  }

  if (level < kMaxLevel && (max >> (kWidthBits * level + 1)) > 0) {
    return TicksToNextEvent(max, level + 1);
  }

  return max;
}

}  // namespace libz
