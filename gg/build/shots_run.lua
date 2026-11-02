-- Headless screenshots: prints PNG frames as hex lines "SHOT <frame> <hex>" at the given frames.
-- tools/shots.py substitutes 60, 300, 900, 2000 (Mesen's Lua sandbox has no os/io library).
local want = { 60, 300, 900, 2000 }
local last = want[#want]
local f = 0
emu.addEventCallback(function()
  f = f + 1
  for _, w in ipairs(want) do
    if f == w then
      local png = emu.takeScreenshot()
      print("SHOT " .. f .. " " .. (png:gsub(".", function(c) return string.format("%02x", c:byte()) end)))
    end
  end
  if f >= last then emu.stop(0) end
end, emu.eventType.endFrame)
