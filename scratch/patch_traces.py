import sys
import re

file_ic = "c:/Users/toanpq/Desktop/marco/src/core/input_capture.cpp"
with open(file_ic, "r", encoding="utf-8") as f:
    content_ic = f.read()

# Add EVENT_ENQUEUE
content_ic = content_ic.replace(
"""        s_eventQueue[s_eventHead] = ev;
        s_eventHead = next;
        s_eventCv.notify_one();""",
"""        s_eventQueue[s_eventHead] = ev;
        DLOG_TRACE(Hook, "[FIRE_TRACE] EVENT_ENQUEUE gen=%llu", ev.generation_id);
        s_eventHead = next;
        s_eventCv.notify_one();""")

# Add EVENT_DEQUEUE
content_ic = content_ic.replace(
"""        int64_t dequeue_time = timing::NowUs();
        int64_t queue_latency_us = dequeue_time - ev.timestamp_enqueue_us;""",
"""        int64_t dequeue_time = timing::NowUs();
        int64_t queue_latency_us = dequeue_time - ev.timestamp_enqueue_us;
        DLOG_TRACE(Hook, "[FIRE_TRACE] EVENT_DEQUEUE gen=%llu lat=%lld", ev.generation_id, queue_latency_us);""")

with open(file_ic, "w", encoding="utf-8") as f:
    f.write(content_ic)

file_tm = "c:/Users/toanpq/Desktop/marco/src/core/timing.cpp"
with open(file_tm, "r", encoding="utf-8") as f:
    content_tm = f.read()

# Add TIMER_SCHEDULE
content_tm = content_tm.replace(
"""    DLOG_INFO(Timing, "ScheduleTimerAtUs: %s for %lld us target (id=%llu)", reinterpret_cast<int64_t>(keymap::KeyName[ki(key)]), expireUs, id);""",
"""    DLOG_INFO(Timing, "ScheduleTimerAtUs: %s for %lld us target (id=%llu)", reinterpret_cast<int64_t>(keymap::KeyName[ki(key)]), expireUs, id);
    DLOG_TRACE(Timing, "[FIRE_TRACE] TIMER_SCHEDULE id=%llu expireUs=%lld", id, expireUs);""")

with open(file_tm, "w", encoding="utf-8") as f:
    f.write(content_tm)

print("done traces")
