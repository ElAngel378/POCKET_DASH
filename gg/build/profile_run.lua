-- Headless profiler for DEBUG_PROFILE builds.
-- Usage: Mesen.exe --testrunner build/POCKETDASH_prof.gg tools/mesen_profile.lua
-- The ROM writes a section id to gpmark at each step of the main loop:
--   1 scroll  2 objects  3 player  4 camera  5 prepare  6 wait-vblank  7 vram  8 sprites
-- For every loop iteration this records CPU cycles per section and the "busy" time (everything
-- except waiting for vblank). An iteration whose busy time (plus interrupts) exceeds one frame
-- makes the game drop a frame.

-- Mesen's Lua sandbox has no os library, so tools/profile.py substitutes these two values.
local MARK_ADDR = 0x0000C176
local RUN_FRAMES = 4800
local FRAME_CYCLES = 228 * 262            -- 59736 CPU cycles per video frame

local names = { "scroll", "objects", "player", "camera", "prepare", "wait", "vram", "sprites" }
local total, maxc, count = {}, {}, {}
for i = 1, 8 do total[i] = 0; maxc[i] = 0; count[i] = 0 end

local last_mark, last_cyc = nil, nil
local iter_busy = 0
local iterations, slow_iters = 0, 0
local slow_list = {}
local frame = 0
local worst_busy, worst_frame = 0, 0
local iter_sec = {}              -- per-section cycles of the current iteration
local slow_detail = {}
local finished = false
local vram_seen = false          -- mark 7 (vblank reached) seen during the current frame
local lag_frames, lag_list = 0, {}

local function cycles()
  return emu.getState()["cpu.cycleCount"]
end

emu.addMemoryCallback(function(addr, value)
  if value < 1 or value > 8 then return end   -- startup zero-initialisation
  local c = cycles()
  if last_mark ~= nil then
    local d = c - last_cyc
    total[last_mark] = total[last_mark] + d
    count[last_mark] = count[last_mark] + 1
    if d > maxc[last_mark] then maxc[last_mark] = d end
    if last_mark ~= 6 then iter_busy = iter_busy + d end
    iter_sec[last_mark] = (iter_sec[last_mark] or 0) + d
  end
  if value == 1 and last_mark ~= nil then
    -- a new iteration starts: close the previous one
    iterations = iterations + 1
    if iter_busy > worst_busy then worst_busy = iter_busy; worst_frame = frame end
    if iter_busy > FRAME_CYCLES then
      slow_iters = slow_iters + 1
      if #slow_list < 40 then slow_list[#slow_list + 1] = string.format("f%d:%d", frame, iter_busy) end
      if #slow_detail < 8 and iter_busy < 4 * FRAME_CYCLES then
        local parts = {}
        for i = 1, 8 do if i ~= 6 then parts[#parts + 1] = names[i] .. "=" .. (iter_sec[i] or 0) end end
        slow_detail[#slow_detail + 1] = string.format("  f%d  %s", frame, table.concat(parts, " "))
      end
    end
    iter_busy = 0
    iter_sec = {}
  end
  if value == 7 then vram_seen = true end
  last_mark, last_cyc = value, c
end, emu.callbackType.write, MARK_ADDR, MARK_ADDR, emu.cpuType.sms, emu.memType.smsMemory)

emu.addEventCallback(function()
  frame = frame + 1
  if last_mark ~= nil then          -- only count once the main loop is running
    if not vram_seen then
      lag_frames = lag_frames + 1
      if #lag_list < 40 then lag_list[#lag_list + 1] = tostring(frame) end
    end
  end
  vram_seen = false
  if frame >= RUN_FRAMES and not finished then
    finished = true
    print(string.format("frames %d, loop iterations %d, lag frames ~%d, slow iterations %d",
      frame, iterations, frame - iterations, slow_iters))
    print(string.format("frame budget %d cycles; worst busy %d (%.0f%%) at frame %d",
      FRAME_CYCLES, worst_busy, 100 * worst_busy / FRAME_CYCLES, worst_frame))
    for i = 1, 8 do
      if count[i] > 0 then
        print(string.format("  %-8s avg %6d  max %6d cycles", names[i], math.floor(total[i] / count[i]), maxc[i]))
      end
    end
    print(string.format("dropped frames (no game update during the frame): %d", lag_frames))
    print("  at frames: " .. table.concat(lag_list, " "))
    print("slow: " .. table.concat(slow_list, " "))
    print("breakdown of the first slow iterations:")
    for _, l in ipairs(slow_detail) do print(l) end
    emu.stop(0)
  end
end, emu.eventType.endFrame)
