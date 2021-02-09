#!/usr/bin/env tarantool

local fiber = require('fiber')
local net_box = require('net.box')

local fibers= {}
for i = 1, 1e2 do
    fibers[i]=fiber.new(function()
        local tnt = net_box.new("127.0.0.1", 3301)
        for _ = 1, 1e3 do
            pcall(tnt.call, tnt, 'ping')
        end
    end)
    fibers[i]:set_joinable(true)
end

for _, f in ipairs(fibers) do
    f:join()
end
