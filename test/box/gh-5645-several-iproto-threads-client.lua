#!/usr/bin/env tarantool

local fiber = require('fiber')
local net_box = require('net.box')

local fibers= {}
for i = 1, 1e2 do
    fibers[i]=fiber.new(function()
        print("NEW BEFORE")
        local tnt = net_box.new("127.0.0.1", 3301)
         print("NEW AFTER")
        for _ = 1, 1e4 do
            if tnt then
                pcall(tnt.call, tnt, 'ping')
            else
            	print("NO TNT !!!!!!!!!!!!!!!!!!!!!!!!!!!")
            end
        end
    end)
    fibers[i]:set_joinable(true)
end

print("GGGGGGG")

for _, f in ipairs(fibers) do
    print("YES")
    f:join()
end
